#include <cmd.hpp>
#include <pylib.hpp>
#include <python3.14/Python.h>
#include <python_exp_config.hpp>
#include <tool.hpp>

using namespace std::literals;

#define _STR(X) #X
#define STR(X) _STR(X)

#ifdef CONFIG_PYTHON_EXP_HIJACK_REF
#undef Py_INCREF
#define Py_INCREF(op)                                                                                                  \
  do {                                                                                                                 \
    spdlog::trace("Py_INCREF called {}:{}\t({}, {})", __FILE__, __LINE__, #op, static_cast<void *>(op));               \
    Py_INCREF(_PyObject_CAST(op));                                                                                     \
  } while (0)

#undef Py_XDECREF
#define Py_XDECREF(op)                                                                                                 \
  do {                                                                                                                 \
    spdlog::trace("Py_XDECREF called {}:{}\t({}, {})", __FILE__, __LINE__, #op, static_cast<void *>(op));              \
    Py_XDECREF(_PyObject_CAST(op));                                                                                    \
  } while (0)

#undef Py_DECREF
#define Py_DECREF(op)                                                                                                  \
  do {                                                                                                                 \
    spdlog::trace("Py_DECREF called {}:{}\t({}, {})", __FILE__, __LINE__, #op, static_cast<void *>(op));               \
    Py_DECREF(_PyObject_CAST(op));                                                                                     \
  } while (0)

#endif

bool PythonTool::setError(const std::string &message) {
  if (PyErr_Occurred()) {
    PyErr_Print();
  }
  logger->error("Failed to load Python tool '{}': {}", scriptPath.string(), message);
  return false;
}

bool PythonTool::requireCallable(PyObject *dict, const char *key, PyObject *&value) {
  value = PyDict_GetItemString(dict, key);
  if (value == nullptr || !PyCallable_Check(value)) {
    logger->error("Python tool '{}' requires callable '{}'", scriptPath.string(), key);
    return false;
  }
  Py_INCREF(value);
  return true;
}

void PythonTool::releasePythonObjects() noexcept {
  if (!Py_IsInitialized() || interpreterState == nullptr)
    return;
  PyEval_AcquireThread(interpreterState);
  Py_XDECREF(shutdownFunction);
  Py_XDECREF(initializeFunction);
  Py_XDECREF(executeFunction);
  Py_XDECREF(module);
  shutdownFunction = initializeFunction = executeFunction = module = nullptr;
  PyEval_ReleaseThread(interpreterState);
}

bool PythonTool::acquireInterpreter() noexcept {
  if (!Py_IsInitialized() || interpreterState == nullptr)
    return false;
  PyEval_AcquireThread(interpreterState);
  return true;
}

void PythonTool::releaseInterpreter() noexcept {
  if (interpreterState != nullptr)
    PyEval_ReleaseThread(interpreterState);
}

void PythonTool::initialize(initArgs &args) {
  this->logger = args.logger;
  this->parser = args.parser;
  logger->info("Initializing PythonTool with script: {}", scriptPath.string());

  if (!std::filesystem::is_directory(scriptPath) || !std::filesystem::is_regular_file(scriptPath / "__init__.py")) {
    logger->error("Python tool path '{}' must be a directory containing __init__.py", scriptPath.string());
    return;
  }

  if (!Py_IsInitialized()) {
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    PyImport_AppendInittab("explo", &PyInit_pylib);
    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
      return;
    }
  }
  PyGILState_STATE gil = PyGILState_Ensure();
  hostThreadState = PyThreadState_Get();
  interpreterState = Py_NewInterpreter();
  if (interpreterState == nullptr) {
    PyGILState_Release(gil);
    setError("unable to create subinterpreter");
    return;
  }

  do {
    PyObject *sysPath = PySys_GetObject("path");
    PyObject *libPath = PyUnicode_FromString(CONFIG_PYTHON_EXP_LIB_DIR);
    PyObject *parent = PyUnicode_FromString(scriptPath.parent_path().string().c_str());
    if (sysPath == nullptr || libPath == nullptr || parent == nullptr || PyList_Append(sysPath, libPath) != 0 ||
        PyList_Append(sysPath, parent) != 0) {
      Py_XDECREF(libPath);
      Py_XDECREF(parent);
      setError("unable to add the package parent to sys.path");
      break;
    }
    Py_DECREF(libPath);
    Py_DECREF(parent);

    module = PyImport_ImportModule(scriptPath.filename().string().c_str());
    if (module == nullptr) {
      setError("module import failed");
      break;
    }

    PyObject *registerFunction = PyObject_GetAttrString(module, "RegisterTool");
    if (registerFunction == nullptr || !PyCallable_Check(registerFunction)) {
      Py_XDECREF(registerFunction);
      setError("module must define a callable RegisterPlugin function");
      break;
    }

    PyObject *definition = PyObject_CallNoArgs(registerFunction);
    Py_DECREF(registerFunction);
    if (definition == nullptr || !PyDict_Check(definition)) {
      Py_XDECREF(definition);
      setError("RegisterPlugin must return a dict");
      break;
    }

    const char *requiredStrings[] = {"name", "description", "version", "uuid"};
    std::string uuidString;
    std::string *stringTargets[] = {&name, &description, &version, &uuidString};
    // bool valid = true;
    for (size_t i = 0; i < 4; ++i) {
      PyObject *value = PyDict_GetItemString(definition, requiredStrings[i]);
      if (value == nullptr || !PyUnicode_Check(value)) {
        logger->error("Python tool '{}' requires string '{}'", scriptPath.string(), requiredStrings[i]);
        valid = false;
        break;
      }
      const char *stringValue = PyUnicode_AsUTF8(value);
      if (stringValue == nullptr) {
        setError("metadata contains invalid UTF-8");
        valid = false;
        break;
      }
      *stringTargets[i] = stringValue;
    }
    if (valid) {
      try {
        uuid = uuids::uuid::from_string(uuidString).value();
      } catch (const std::exception &e) {
        logger->error("Failed to parse UUID '{}' for Python tool '{}': {}", uuidString, scriptPath.string(), e.what());
        valid = false;
      }
    }
    PyObject *tagsObject = PyDict_GetItemString(definition, "tags");
    if (valid && (tagsObject == nullptr || !PyList_Check(tagsObject))) {
      logger->error("Python tool '{}' requires list 'tags'", scriptPath.string());
      valid = false;
    }
    if (valid) {
      for (Py_ssize_t i = 0; i < PyList_Size(tagsObject); ++i) {
        PyObject *tag = PyList_GetItem(tagsObject, i);
        if (!PyUnicode_Check(tag)) {
          logger->error("Python tool '{}' requires tags to contain strings", scriptPath.string());
          valid = false;
          break;
        }
        const char *tagValue = PyUnicode_AsUTF8(tag);
        if (tagValue == nullptr) {
          setError("tags contains invalid UTF-8");
          valid = false;
          break;
        }
        tags.emplace_back(tagValue);
      }
    }
    PyObject *vars = PyDict_GetItemString(definition, "vars");
    PyObject *desc = PyDict_GetItemString(definition, "descs");
    PyObject *root = PyDict_GetItemString(definition, "rootRequired");
    PyObject *command = PyDict_GetItemString(definition, "makeCommand");
    if (valid && (vars == nullptr || !PyDict_Check(vars) || desc == nullptr || !PyDict_Check(desc) || root == nullptr ||
                  !PyBool_Check(root) || command == nullptr || !PyBool_Check(command))) {
      logger->error("Python tool '{}' has invalid vars, desc, rootRequried, or makeCommand metadata",
                    scriptPath.string());
      logger->trace("vars: {}, desc: {}, root: {}, command: {} valid: {}", vars ? "present" : "missing",
                    desc ? "present" : "missing", root ? "present" : "missing", command ? "present" : "missing", valid);
      logger->trace("vars type: {}, desc type: {}, root type: {}, command type: {}",
                    vars ? Py_TYPE(vars)->tp_name : "missing", desc ? Py_TYPE(desc)->tp_name : "missing",
                    root ? Py_TYPE(root)->tp_name : "missing", command ? Py_TYPE(command)->tp_name : "missing");
      valid = false;
    }
    if (valid) {
      rootRequired = PyObject_IsTrue(root);
      makeCommand = PyObject_IsTrue(command);
      valid = requireCallable(definition, "execute", executeFunction) &&
              requireCallable(definition, "initialize", initializeFunction) &&
              requireCallable(definition, "shutdown", shutdownFunction);
      Py_ssize_t pos = 0;
      PyObject *key, *value;
      while (PyDict_Next(vars, &pos, &key, &value)) {
        const char *keyStr = PyUnicode_AsUTF8(key);
        if (!keyStr) {
          setError("vars contains invalid UTF-8 key");
          valid = false;
          break;
        }
        const char *descStr = nullptr;
        if (PyDict_Contains(desc, key)) {
          PyObject *descValue = PyDict_GetItem(desc, key);
          descStr = PyUnicode_AsUTF8(descValue);
          if (!descStr) {
            setError("desc contains invalid UTF-8 value for key: " + std::string(keyStr));
            valid = false;
            break;
          }
        }
        PythonType value_;
        if (PyUnicode_Check(value)) {
          value_ = PyUnicode_AsUTF8(value);
          if (!value_.data.index()) {
            setError("vars contains invalid UTF-8 value for key: " + std::string(keyStr));
            valid = false;
            break;
          }
        } else if (PyLong_Check(value)) {
          value_ = PyLong_AsLongLong(value);
        } else if (PyFloat_Check(value)) {
          value_ = PyFloat_AsDouble(value);
        } else if (PyBool_Check(value)) {
          value_ = PyObject_IsTrue(value);
        } else {
          logger->warn("Python variable: '{}' of type '{}' cannot be set", keyStr, Py_TYPE(value)->tp_name);
          continue;
        }
        this->vars[keyStr] = std::move(value_);
        this->descs[keyStr] = descStr ? descStr : "";
      }
    }

    Py_DECREF(definition);
    if (!valid)
      break;

    PyObject *result = PyObject_CallNoArgs(initializeFunction);
    if (result == nullptr) {
      setError("initialize function failed");
      break;
    }
    Py_DECREF(result);
    loaded = true;
    logger->info("Initialized Python tool: {} v{}", name, version);
  } while (false);

  PyThreadState_Swap(hostThreadState);
  PyGILState_Release(gil);
  // PyGILState_Release may leave the host state attached to this thread.
  // Subinterpreter states must be acquired only from a thread with no state.
  PyThreadState_Swap(nullptr);

  {
    auto ctx = std::make_shared<cmd::Context>(this->getName());
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run " + name + "'s tool main function";
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        this->execute();
        return "Tool execution completed";
      };
      ctx->registerCommand(c);
    }
    cmd::CommandProcessor::instance().registerContext(ctx);
  }

  if (makeCommand) {
    this->cmd.emplace(*args.parser, this->name, "Run " + this->name + "'s tool main function", [&](args::Subparser &s) {
      if (!acquireInterpreter())
        return;
      flags.reserve(this->vars.size());
      for (const auto &[key, value] : this->vars) {
        const char *keyStr = key.c_str();
        const char *descStr = this->descs.count(key) ? this->descs.at(key).c_str() : nullptr;
        if (value.is<std::string>()) {
          flags.emplace_back(std::make_unique<args::ValueFlag<std::string>>(
                                 s, keyStr, descStr ? descStr : "", args::Matcher{key}, value.as<std::string>()),
                             PythonFlagtype::STRING);
        } else if (value.is<int64_t>()) {
          flags.emplace_back(std::make_unique<args::ValueFlag<int64_t>>(s, keyStr, descStr ? descStr : "",
                                                                        args::Matcher{key}, value.as<int64_t>()),
                             PythonFlagtype::INTEGER);
        } else if (value.is<double>()) {
          flags.emplace_back(std::make_unique<args::ValueFlag<double>>(s, keyStr, descStr ? descStr : "",
                                                                       args::Matcher{key}, value.as<double>()),
                             PythonFlagtype::NUMBER);
        } else if (value.is<bool>()) {
          flags.emplace_back(std::make_unique<args::ValueFlag<bool>>(s, keyStr, descStr ? descStr : "",
                                                                     args::Matcher{key}, value.as<bool>()),
                             PythonFlagtype::BOOLEAN);
        } else {
          logger->warn("Python variable: '{}' of type '' cannot be set via command line", keyStr);
        }
      }
      s.Parse();
      if (s.GetError() == args::Error::Help) {
        std::cout << s;
        this->flags.clear();
        return;
      }
      varTypes.reserve(flags.size());
      for (const auto &[flag, type] : flags) {
        switch (type) {
        case PythonFlagtype::STRING: {
          auto val = dynamic_cast<args::ValueFlag<std::string> *>(flag.get());
          if (val)
            varTypes[val->Name()] = val->Get();
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<std::string>");
          break;
        }
        case PythonFlagtype::NUMBER: {
          auto valNum = dynamic_cast<args::ValueFlag<double> *>(flag.get());
          if (valNum)
            varTypes[valNum->Name()] = valNum->Get();
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<double>");
          break;
        }
        case PythonFlagtype::INTEGER: {
          auto valInt = dynamic_cast<args::ValueFlag<int64_t> *>(flag.get());
          if (valInt)
            varTypes[valInt->Name()] = valInt->Get();
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<int64_t>");
          break;
        }
        case PythonFlagtype::BOOLEAN: {
          auto valBool = dynamic_cast<args::ValueFlag<bool> *>(flag.get());
          if (valBool)
            varTypes[valBool->Name()] = valBool->Get();
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<bool>");
          break;
        }
        default:
          throw std::runtime_error("Unknown PythonFlagtype");
        }
      }
      flags.clear();
      releaseInterpreter();
    });
  }
}

void PythonTool::shutdown() {
  logger->info("Shutting down PythonTool with script: {}", scriptPath.string());
  logger->trace("Python is initialized: {}", Py_IsInitialized());
  if (loaded && acquireInterpreter()) {
    PyObject *result = PyObject_CallNoArgs(shutdownFunction);
    if (result == nullptr)
      setError("shutdown function failed");
    else
      Py_DECREF(result);
    releaseInterpreter();
  }
  releasePythonObjects();
  if (interpreterState != nullptr) {
    logger->trace("Releasing Python subinterpreter state");
    PyEval_AcquireThread(interpreterState);
    PyThreadState *state = interpreterState;
    Py_EndInterpreter(state);
    interpreterState = nullptr;

    if (hostThreadState != nullptr) {
      PyEval_AcquireThread(hostThreadState);
      PyEval_ReleaseThread(hostThreadState);
    }
    hostThreadState = nullptr;
  }
}

void PythonTool::execute() {
  if (!loaded || !acquireInterpreter())
    return;
  PyObject *result = PyObject_CallNoArgs(executeFunction);
  if (result == nullptr)
    setError("execute function failed");
  else
    Py_DECREF(result);
  releaseInterpreter();
}

void PythonTool::invoke(std::string_view prefix, bool soft) {
  this->prefix = prefix;
  logger->info("Invoking PythonTool with script: {} and prefix: {}", scriptPath.string(), prefix);
  logger->trace("Python is initialized: {}", Py_IsInitialized());
  auto &vars = cmd::CommandProcessor::instance().vars();
  for (const auto &[key, value] : this->vars) {
    if (soft && vars.get(this->prefix + "." + key).has_value()) {
      logger->info("Python variable: '{}' already set, skipping", this->prefix + "." + key);
      continue;
    }
    if (value.is<std::string>()) {
      logger->info("Python variable: '{}' = '{}'", key, value.as<std::string>());
      vars.set(prefix + "."s + key, cmd::VarValue(value.as<std::string>()));
    } else if (value.is<double>()) {
      logger->info("Python variable: '{}' = {}", key, value.as<double>());
      vars.set(prefix + "."s + key, cmd::VarValue(value.as<double>()));
    } else if (value.is<int64_t>()) {
      logger->info("Python variable: '{}' = {}", key, value.as<int64_t>());
      vars.set(prefix + "."s + key, cmd::VarValue(value.as<int64_t>()));
    } else if (value.is<bool>()) {
      logger->info("Python variable: '{}' = {}", key, value.as<bool>());
      vars.set(prefix + "."s + key, cmd::VarValue(value.as<bool>()));
    } else {
      logger->info("Unused python variable: '{}' = '<ommitted>'", key);
    }
  }
}

void PythonTool::suppress() {
  logger->info("Suppressing PythonTool with script: {}", scriptPath.string());
  auto &vars = cmd::CommandProcessor::instance().vars();
  for (const auto &[key, value] : this->vars) {
    if (vars.get(this->prefix + "." + key).has_value()) {
      logger->info("Removing Python variable: '{}'", this->prefix + "." + key);
      vars.unset(this->prefix + "." + key);
    } else {
      logger->info("Python variable: '{}' not set, skipping removal", this->prefix + "." + key);
    }
  }
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
