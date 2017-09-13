/*
 * QMP interface for instrumentation control commands.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <dlfcn.h>

#include "instrument/load.h"
#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qmp-commands.h"


InstrLoadResult *qmp_instr_load(const char *path,
                                bool has_id, const char *id,
                                bool have_args, strList *args,
                                Error **errp)
{
    InstrLoadResult *res = g_malloc0(sizeof(*res));
    int argc = 0;
    const char **argv = NULL;
    InstrLoadError code;

    if (!has_id) {
        id = NULL;
    }

    strList *entry = have_args ? args : NULL;
    while (entry != NULL) {
        argv = realloc(argv, sizeof(*argv) * (argc + 1));
        argv[argc] = entry->value;
        argc++;
        entry = entry->next;
    }

    code = instr_load(path, argc, argv, &id);
    switch (code) {
    case INSTR_LOAD_OK:
        res->id = g_strdup(id);
        break;
    case INSTR_LOAD_ID_EXISTS:
        error_setg(errp, "Library ID exists");
        break;
    case INSTR_LOAD_TOO_MANY:
        error_setg(errp, "Tried to load too many libraries");
        break;
    case INSTR_LOAD_ERROR:
        error_setg(errp, "Library initialization returned non-zero");
        break;
    case INSTR_LOAD_DLERROR:
        error_setg(errp, "Error loading library: %s",
                   dlerror());
        break;
    }

    if (*errp) {
        g_free(res);
        res = NULL;
    }

    return res;
}

void qmp_instr_unload(const char *id, Error **errp)
{
    InstrUnloadError code = instr_unload(id);
    switch (code) {
    case INSTR_UNLOAD_OK:
        break;
    case INSTR_UNLOAD_INVALID:
        error_setg(errp, "Unknown library ID");
        break;
    case INSTR_UNLOAD_DLERROR:
        error_setg(errp, "Error unloading library: %s", dlerror());
        break;
    }
}
