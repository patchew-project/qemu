/*
 * Copyright (c) 2021, 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "migration/cpr.h"
#include "migration/cpr-state.h"
#include "sysemu/runstate.h"

bool only_cpr_capable;
static Notifier cpr_fd_notifier;

static int preserve_fd(const char *name, int id, int fd, void *opaque)
{
    qemu_clear_cloexec(fd);
    return 0;
}

static int unpreserve_fd(const char *name, int id, int fd, void *opaque)
{
    qemu_set_cloexec(fd);
    return 0;
}

static void cpr_fd_notifier_func(Notifier *notifier, void *data)
{
    MigrationState *s = data;

    if (migrate_mode_of(s) == MIG_MODE_CPR_EXEC && migration_has_failed(s)) {
        cpr_walk_fd(unpreserve_fd, 0);
    }
}

void cpr_preserve_fds(void)
{
    cpr_walk_fd(preserve_fd, 0);
}

void cpr_init(void)
{
    cpr_state_load(&error_fatal);
    migration_add_notifier(&cpr_fd_notifier, cpr_fd_notifier_func);
}

void cpr_exec(void)
{
    MigrationState *s = migrate_get_current();
    Error *err = NULL;

    if (migrate_mode_of(s) == MIG_MODE_CPR_EXEC && !migration_has_failed(s)) {
        if (!migration_has_finished(s)) {
            error_setg(&err, "cannot exec: migration status is '%s', "
                             "but must be 'completed'",
                       MigrationStatus_str(s->state));
            goto error;
        }

        if (cpr_state_save(&err)) {
            goto error;
        }

        qemu_system_exec_request(s->parameters.cpr_exec_args);
    }
    return;

error:
    cpr_exec_failed(err);
}

void cpr_exec_failed(Error *err)
{
    MigrationState *s = migrate_get_current();

    migrate_set_state(&s->state, s->state, MIGRATION_STATUS_FAILED);
    migrate_set_error(s, err);
    error_report_err(err);
    migration_call_notifiers(s);
    cpr_state_unsave();
}
