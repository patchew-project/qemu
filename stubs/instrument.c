/*
 * Instrumentation placeholders.
 *
 * Copyright (C) 2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "instrument/cmdline.h"
#include "instrument/control.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"


/* Declare missing types */
typedef struct strList strList;


void instr_init(const char *path, int argc, const char **argv)
{
}
void instr_fini(void)
{
}

InstrLoadResult *qmp_instr_load(const char *path,
                                bool has_id, const char *id,
                                bool have_args, strList *args,
                                Error **errp);
InstrLoadResult *qmp_instr_load(const char *path,
                                bool has_id, const char *id,
                                bool have_args, strList *args,
                                Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
void qmp_instr_unload(const char *id, Error **errp);
void qmp_instr_unload(const char *id, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}


__thread InstrState instr_cur_state;
