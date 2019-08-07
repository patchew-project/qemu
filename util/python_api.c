#include "sysemu/python_api.h"
#include "qemu/osdep.h"

PyObject *python_callback(const char *abs_module_path, const char *mod,
                          const char *func, char *args[], const int nargs)
{
    PyObject *mod_name, *module, *mod_ref, *function, *arguments;
    PyObject *result = 0;
    PyObject *value = NULL;

    /* Set PYTHONPATH to absolute module path directory */
    if (!abs_module_path)
        abs_module_path = ".";
    setenv("PYTHONPATH", abs_module_path, 1);

    /* Initialize the Python Interpreter */
    Py_Initialize();
    mod_name = PyUnicode_FromString(mod);
    /* Import module object */
    module = PyImport_Import(mod_name);
    if (!module) {
        PyErr_Print();
        fprintf(stderr, "Failed to load \"%s\"\n", mod);
        exit(EXIT_FAILURE);
    }
    mod_ref = PyModule_GetDict(module);
    function = PyDict_GetItemString(mod_ref, func);
    if (function && PyCallable_Check(function)) {
        arguments = PyTuple_New(nargs);
        for (int i = 0; i < nargs; i++) {
            value = PyUnicode_FromString(args[i]);
            if (!value) {
                Py_DECREF(arguments);
                Py_DECREF(module);
                fprintf(stderr, "Cannot convert argument\n");
                exit(EXIT_FAILURE);
            }
            PyTuple_SetItem(arguments, i, value);
        }
        PyErr_Print();
        result = PyObject_CallObject(function, arguments);
        PyErr_Print();
    }
    else {
        if (PyErr_Occurred())
            PyErr_Print();
        fprintf(stderr, "Cannot find function \"%s\"\n", func);
        exit(EXIT_FAILURE);
    }
    /* Clean up */
    Py_DECREF(value);
    Py_DECREF(module);
    Py_DECREF(mod_name);
    /* Finish the Python Interpreter */
    Py_Finalize();
    return result;
}

uint64_t python_callback_int(const char *abs_module_path, const char *mod,
                             const char *func, char *args[], const int nargs)
{
    PyObject *result;
    result = python_callback(abs_module_path, mod, func, args, nargs);
    return PyLong_AsLong(result);
}

char *python_callback_str(const char *abs_module_path, const char *mod,
                          const char *func, char *args[], const int nargs)
{
    PyObject *result;
    result = python_callback(abs_module_path, mod, func, args, nargs);
    return PyUnicode_AsUTF8(result);
}

bool python_callback_bool(const char *abs_module_path, const char *mod,
                          const char *func, char *args[], const int nargs)
{
    PyObject *result;
    result = python_callback(abs_module_path, mod, func, args, nargs);
    return (result == Py_True);
}

void python_args_init_cast_int(char *args[], int arg, int pos)
{
    args[pos]= malloc(sizeof(int));
    sprintf(args[pos], "%d", arg);
}

void python_args_init_cast_long(char *args[], uint64_t arg, int pos)
{
    args[pos]= g_malloc(sizeof(uint64_t) * 2);
    sprintf(args[pos], "%lx", arg);
}

void python_args_clean(char *args[], int nargs)
{
    for (int i = 0; i < nargs; i++) {
        g_free(args[i]);
    }
}
