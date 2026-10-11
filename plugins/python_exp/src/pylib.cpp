#include <pylib.hpp>

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
PyMODINIT_FUNC PyInit_pylib() {
  // clang-format on
  return PyModule_Create2(&pylib_module, PYTHON_API_VERSION);
}