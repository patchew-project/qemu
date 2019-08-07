#ifndef _PPC_PNV_PYTHON_H
#define _PPC_PNV_PYTHON_H

#include <stdbool.h>
#include <Python.h>

extern PyObject *python_callback(const char *abs_module_path, const char *mod,
                                 const char *func, char *args[],
                                 const int nargs);

extern uint64_t python_callback_int(const char *abs_module_path,
                                    const char *mod,
                                    const char *func, char *args[],
                                    const int nargs);

extern char *python_callback_str(const char *abs_module_path, const char *mod,
                                 const char *func, char *args[],
                                 const int nargs);

extern bool python_callback_bool(const char *abs_module_path, const char *mod,
                                 const char *func, char *args[],
                                 const int nargs);

extern void python_args_init_cast_int(char *args[], int arg, int pos);

extern void python_args_init_cast_long(char *args[], uint64_t arg, int pos);

extern void python_args_clean(char *args[], int nargs);

#endif
