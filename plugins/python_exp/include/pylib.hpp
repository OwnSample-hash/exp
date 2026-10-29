#pragma once
#include <python3.14/Python.h>
#include <spdlog/spdlog.h>

#define pyLogFuncs                                                                                                     \
  X(trace, METH_VARARGS, "Log trace message")                                                                          \
  X(debug, METH_VARARGS, "Log debug message")                                                                          \
  X(info, METH_VARARGS, "Log info message")                                                                            \
  X(warn, METH_VARARGS, "Log warn message")                                                                            \
  X(error, METH_VARARGS, "Log error message")

#define pyFuncs                                                                                                        \
  X(var, METH_VARARGS, "Access variable")                                                                              \
  X(vars, METH_NOARGS, "Get all variables")                                                                            \
  pyLogFuncs

#define X(name, ...) PyObject *Py_##name(PyObject *self, PyObject *args);
pyFuncs
#undef X

    static PyMethodDef pylib_methods[] = {
#define X(name, type, doc) {#name, Py_##name, type, doc},
        pyFuncs
#undef X
        {NULL, NULL, 0, NULL} // Sentinel
};

static PyModuleDef pylib_module = {
    PyModuleDef_HEAD_INIT, "explo", "Explo Python Module", -1, pylib_methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit_pylib();
