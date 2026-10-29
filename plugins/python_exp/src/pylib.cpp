#include <cmd.hpp>
#include <pylib.hpp>
#include <pytypedefs.h>

static auto &getLogger() {
  static std::shared_ptr<spdlog::logger> logger;
  if (!logger)
    logger = spdlog::get("python_exp")->clone("python_exp::lib");
  return logger;
}

#define X(name, ...)                                                                                                   \
  PyObject *Py_##name(PyObject *self, PyObject *args) {                                                                \
    const char *log_msg;                                                                                               \
    if (!PyArg_ParseTuple(args, "s", &log_msg)) {                                                                      \
      PyErr_SetString(PyExc_TypeError, "Invalid arguments for " #name);                                                \
      return nullptr;                                                                                                  \
    }                                                                                                                  \
    getLogger()->name("[Python] {}", log_msg);                                                                         \
    return Py_None;                                                                                                    \
  }
pyLogFuncs
#undef X

    // clang-format off
PyObject *Py_var(PyObject *self, PyObject *args) {
  // clang-format on
  const char *var_name;
  if (!PyArg_ParseTuple(args, "s", &var_name)) {
    PyErr_SetString(PyExc_TypeError, "Invalid arguments for var");
    return nullptr;
  }
  auto &vars = explo::cmd::CommandProcessor::instance().vars();
  auto value = vars.get(var_name);
  if (!value.has_value()) {
    PyErr_SetString(PyExc_KeyError, "Variable not found");
    return nullptr;
  }
  switch (value->type) {
  case explo::cmd::VarType::String:
    return PyUnicode_FromString(value->sval.c_str());
  case explo::cmd::VarType::Integer:
    return PyLong_FromLongLong(value->ival);
  case explo::cmd::VarType::Float:
    return PyFloat_FromDouble(value->fval);
  case explo::cmd::VarType::Bool:
    return PyBool_FromLong(value->ival);
  case explo::cmd::VarType::Array: {
    PyObject *list = PyList_New(value->aval.size());
    for (size_t i = 0; i < value->aval.size(); ++i) {
      PyObject *item = PyUnicode_FromString(value->aval[i].c_str());
      PyList_SetItem(list, i, item); // Steals reference to item
    }
    return list;
  }
  }
}

PyObject *Py_vars(PyObject *self, PyObject *args) {
  auto &vars = explo::cmd::CommandProcessor::instance().vars();
  PyObject *dict = PyDict_New();
  for (const auto &names : vars.names()) {
    auto value = vars.get(names);
    if (!value.has_value())
      continue;
    PyObject *py_value = nullptr;
    switch (value->type) {
    case explo::cmd::VarType::String:
      py_value = PyUnicode_FromString(value->sval.c_str());
      break;
    case explo::cmd::VarType::Integer:
      py_value = PyLong_FromLongLong(value->ival);
      break;
    case explo::cmd::VarType::Float:
      py_value = PyFloat_FromDouble(value->fval);
      break;
    case explo::cmd::VarType::Bool:
      py_value = PyBool_FromLong(value->ival);
      break;
    case explo::cmd::VarType::Array: {
      py_value = PyList_New(value->aval.size());
      for (size_t i = 0; i < value->aval.size(); ++i) {
        PyObject *item = PyUnicode_FromString(value->aval[i].c_str());
        PyList_SetItem(py_value, i, item); // Steals reference to item
      }
      break;
    }
    }
    if (py_value)
      PyDict_SetItemString(dict, names.c_str(), py_value);
  }
  return dict;
}

// clang-format off
PyMODINIT_FUNC PyInit_pylib() {
  // clang-format on
  return PyModule_Create2(&pylib_module, PYTHON_API_VERSION);
}
