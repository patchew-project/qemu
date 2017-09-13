/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <dlfcn.h>
#include "instrument/cmdline.h"
#include "instrument/load.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"


QemuOptsList qemu_instr_opts = {
    .name = "instrument",
    .implied_opt_name = "file",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_instr_opts.head),
    .desc = {
        {
            .name = "file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "arg",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

void instr_opt_parse(const char *optarg, char **path,
                     int *argc, const char ***argv)
{
    const char *arg;
    QemuOptsIter iter;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("instrument"),
                                             optarg, true);
    if (!opts) {
        exit(1);
    } else {
#if !defined(CONFIG_INSTRUMENT)
        error_report("instrumentation not enabled on this build");
        exit(1);
#endif
    }


    arg = qemu_opt_get(opts, "file");
    if (arg != NULL) {
        g_free(*path);
        *path = g_strdup(arg);
    }

    qemu_opt_iter_init(&iter, opts, "arg");
    while ((arg = qemu_opt_iter_next(&iter)) != NULL) {
        *argv = realloc(*argv, sizeof(**argv) * (*argc + 1));
        (*argv)[*argc] = g_strdup(arg);
        (*argc)++;
    }

    qemu_opts_del(opts);
}

void instr_init(const char *path, int argc, const char **argv)
{
#if defined(CONFIG_INSTRUMENT)
    InstrLoadError err;

    if (path == NULL) {
        return;
    }

    if (atexit(instr_fini) != 0) {
        fprintf(stderr, "error: atexit: %s\n", strerror(errno));
        abort();
    }

    const char *id = "cmdline";
    err = instr_load(path, argc, argv, &id);
    switch (err) {
    case INSTR_LOAD_OK:
        error_report("instrument: loaded library with ID '%s'", id);
        return;
    case INSTR_LOAD_TOO_MANY:
        error_report("instrument: tried to load too many libraries");
        break;
    case INSTR_LOAD_ID_EXISTS:
        g_assert_not_reached();
        break;
    case INSTR_LOAD_ERROR:
        error_report("instrument: library initialization returned non-zero");
        break;
    case INSTR_LOAD_DLERROR:
        error_report("instrument: error loading library: %s", dlerror());
        break;
    }
#else
    error_report("instrument: not available");
#endif

    exit(1);
}

void instr_fini(void)
{
#if defined(CONFIG_INSTRUMENT)
    InstrUnloadError err = instr_unload_all();

    switch (err) {
    case INSTR_UNLOAD_OK:
        return;
    case INSTR_UNLOAD_INVALID:
        /* the user might have already unloaded it */
        return;
    case INSTR_UNLOAD_DLERROR:
        error_report("instrument: error unloading library: %s", dlerror());
        break;
    }
#else
    error_report("instrument: not available");
#endif

    exit(1);
}
