/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef INSTRUMENT__CMDLINE_H
#define INSTRUMENT__CMDLINE_H

#include "qemu/typedefs.h"


/**
 * Definition of QEMU options describing instrumentation subsystem
 * configuration.
 */
extern QemuOptsList qemu_instr_opts;

/**
 * instr_opt_parse:
 * @optarg: A string argument of --instrument command line argument
 *
 * Initialize instrument subsystem.
 */
void instr_opt_parse(const char *optarg, char **path,
                     int *argc, const char ***argv);

/**
 * instr_init:
 * @path: Path to dynamic trace instrumentation library.
 * @argc: Number of arguments to the library's #qi_init routine.
 * @argv: Arguments to the library's #qi_init routine.
 *
 * Load and initialize the given instrumentation library. Calls exit() if the
 * library's initialization function returns a non-zero value.
 *
 * Installs instr_fini() as an atexit() callback.
 */
void instr_init(const char *path, int argc, const char **argv);

/**
 * instr_fini:
 *
 * Deinitialize and unload all instrumentation libraries.
 */
void instr_fini(void);

#endif  /* INSTRUMENT__CMDLINE_H */
