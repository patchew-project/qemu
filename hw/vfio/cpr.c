/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-common.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"

static void vfio_cpr_reboot_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;

    if (migrate_mode_of(s) == MIG_MODE_CPR_REBOOT &&
        !migration_has_failed(s) &&
        !migration_has_finished(s) &&
        !runstate_check(RUN_STATE_SUSPENDED)) {

        Error *err = NULL;
        error_setg(&err, "VFIO device only supports cpr-reboot for "
                         "runstate suspended");
        migrate_set_error(s, err);
        error_free(err);
    }
}

int vfio_cpr_register_container(VFIOContainer *container, Error **errp)
{
    migration_add_notifier(&container->cpr_reboot_notifier,
                           vfio_cpr_reboot_notifier);
    return 0;
}

void vfio_cpr_unregister_container(VFIOContainer *container)
{
    migration_remove_notifier(&container->cpr_reboot_notifier);
}
