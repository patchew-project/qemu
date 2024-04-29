/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/ramblock.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "migration/vmstate.h"
#include "sysemu/runstate.h"
#include "trace.h"

/*************************************************************************/
#define CPR_STATE "CprState"

typedef struct CprState {
    MigMode mode;
} CprState;

static CprState cpr_state = {
    .mode = MIG_MODE_NORMAL,
};

static int cpr_state_presave(void *opaque)
{
    cpr_state.mode = migrate_mode();
    return 0;
}

bool cpr_needed_for_exec(void *opaque)
{
    return migrate_mode() == MIG_MODE_CPR_EXEC;
}

static const VMStateDescription vmstate_cpr_state = {
    .name = CPR_STATE,
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cpr_needed_for_exec,
    .pre_save = cpr_state_presave,
    .precreate = true,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(mode, CprState),
        VMSTATE_END_OF_LIST()
    }
};

vmstate_register_init(NULL, 0, vmstate_cpr_state, &cpr_state);

/*************************************************************************/

typedef int (*cpr_walk_fd_cb)(int fd);

static int walk_ramblock(FactoryObject *obj, void *opaque)
{
    RAMBlock *rb = obj->opaque;
    cpr_walk_fd_cb cb = opaque;
    return cb(rb->fd);
}

static int cpr_walk_fd(cpr_walk_fd_cb cb)
{
    int ret = vmstate_walk_factory_outgoing(RAM_BLOCK, walk_ramblock, cb);
    return ret;
}

static int preserve_fd(int fd)
{
    qemu_clear_cloexec(fd);
    return 0;
}

static int unpreserve_fd(int fd)
{
    qemu_set_cloexec(fd);
    return 0;
}

static void cpr_preserve_fds(void)
{
    cpr_walk_fd(preserve_fd);
}

void cpr_unpreserve_fds(void)
{
    cpr_walk_fd(unpreserve_fd);
}

static int cpr_fd_notifier_func(NotifierWithReturn *notifier,
                                 MigrationEvent *e, Error **errp)
{
    if (migrate_mode() == MIG_MODE_CPR_EXEC &&
        e->type == MIG_EVENT_PRECOPY_FAILED) {
        cpr_unpreserve_fds();
    }
    return 0;
}

void cpr_mig_init(void)
{
    static NotifierWithReturn cpr_fd_notifier;

    migrate_get_current()->parameters.mode = cpr_state.mode;
    migration_add_notifier(&cpr_fd_notifier, cpr_fd_notifier_func);
}

void cpr_exec(char **argv)
{
    MigrationState *s = migrate_get_current();
    Error *err = NULL;

    /*
     * Clear the close-on-exec flag for all preserved fd's.  We cannot do so
     * earlier because they should not persist across miscellaneous fork and
     * exec calls that are performed during normal operation.
     */
    cpr_preserve_fds();

    execvp(argv[0], argv);

    error_setg_errno(&err, errno, "execvp %s failed", argv[0]);
    error_report_err(err);
    migrate_set_state(&s->state, s->state, MIGRATION_STATUS_FAILED);
    migrate_set_error(s, err);
    migration_precreate_unsave();
}
