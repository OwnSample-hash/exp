#include <cmd.hpp>
#include <interactive.hpp>
#include <plugin_interface.hpp>
#include <pylib.hpp>
#include <python_exp.hpp>
#include <string>
#include <termios.h>

using namespace std::string_literals;

#define _STR(X) #X
#define STR(X) _STR(X)

void InteractivePythonTool::initialize(initArgs &args) {
  this->logger = args.logger;
  this->parser = args.parser;

  if (!Py_IsInitialized()) {
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.module_search_paths_set = 1;
    config.buffered_stdio = 0;
    PyWideStringList_Append(&config.module_search_paths, L"");
    PyWideStringList_Append(&config.module_search_paths,
                            L"/usr/lib/python" STR(PY_MAJOR_VERSION) "." STR(PY_MINOR_VERSION));
    PyWideStringList_Append(&config.module_search_paths,
                            L"/usr/lib/python" STR(PY_MAJOR_VERSION) "." STR(PY_MINOR_VERSION) "/lib-dynload");
    PyWideStringList_Append(&config.module_search_paths,
                            L"/usr/lib/python" STR(PY_MAJOR_VERSION) "." STR(PY_MINOR_VERSION) "/site-packages");
    PyWideStringList_Append(&config.module_search_paths, L"" CONFIG_PYTHON_EXP_LIB_DIR);
    PyImport_AppendInittab("explo", &PyInit_pylib);
    PyStatus status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
      PyConfig_Clear(&config);
      logger->error("Failed to initialize Python interpreter");
      return;
    }
    PyConfig_Clear(&config);
    ownsInterpreter = true;
  }

  {
    auto ctx = std::make_shared<cmd::Context>(this->getName());
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run "s + this->getName() + "'s tool main function";
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        this->execute();
        return "Tool execution completed";
      };
      ctx->registerCommand(c);
    }
    cmd::CommandProcessor::instance().registerContext(ctx);
  }

  this->cmd.emplace(*args.parser, this->getName(), "Run the interactive python tool",
                    [&](args::Subparser &s) { s.Parse(); });
}

void InteractivePythonTool::shutdown() {
  logger->info("Shutting down InteractivePythonTool");
  if (ownsInterpreter && Py_IsInitialized()) {
    logger->trace("Python is initialized");
    PyGILState_STATE gil = PyGILState_Ensure();
    int status = Py_FinalizeEx();
    if (status < 0) {
      logger->error("Failed to finalize Python interpreter");
    }
  }
}

const char *helper = "import code\n"
                     "import sys\n"
                     "sys.ps1 = '>>> '\n"
                     "sys.ps2 = '... '\n"
                     "def run_repl(ns):\n"
                     "    try:\n"
                     "        code.interact(banner='Interactive Python Interpreter\\nLogging via explo module can be "
                     "found in the python_exp\\'s log file', local=ns, exitmsg='Exiting Interactive')\n"
                     "    except SystemExit:\n"
                     "        pass\n";

void InteractivePythonTool::execute() {
  logger->info("Executing InteractivePythonTool");
  if (!Py_IsInitialized()) {
    logger->error("Python interpreter is not initialized");
    return;
  }
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag |= (ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  PyGILState_STATE gil = PyGILState_Ensure();
  PyObject *mainModule = PyImport_AddModule("__main__");
  PyObject *ns = PyModule_GetDict(mainModule);
  PyObject *result = PyRun_String(helper, Py_file_input, ns, ns);
  if (result == nullptr) {
    logger->error("Failed to execute helper code:");
  } else {
    Py_DECREF(result);
    PyObject *runReplFunc = PyDict_GetItemString(ns, "run_repl");
    if (runReplFunc && PyCallable_Check(runReplFunc)) {
      PyObject *replResult = PyObject_CallFunction(runReplFunc, "O", ns);
      if (replResult == nullptr) {
        logger->error("Failed to start interactive REPL:");
      } else {
        Py_DECREF(replResult);
      }
    } else {
      logger->error("Failed to find 'run_repl' function in namespace");
    }
  }
  clearerr(stdin);
  PyGILState_Release(gil);
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

void InteractivePythonTool::invoke(std::string_view prefix, bool soft) {
  logger->info("Invoking InteractivePythonTool with prefix: {}", prefix);
  this->prefix = prefix;
}

void InteractivePythonTool::suppress() {
  logger->info("Suppressing InteractivePythonTool");
  // No specific suppression logic for the interactive tool
}

bool InteractivePythonTool::cmdCheck() {
  if (cmd && *cmd) {
    this->invoke(this->prefix, false);
    this->execute();
    this->suppress();
    return true;
  }
  return false;
}

static ToolRegistry::Add<InteractivePythonTool> interactiveTool("interactive");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
