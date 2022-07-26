/*
 * Copyright (c) 2021, 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/misc.h"

void migration_add_notifier(Notifier *notify,
                            void (*cb)(Notifier *notifier, void *data))
{
}

void migration_remove_notifier(Notifier *notify)
{
}

bool migration_has_finished(MigrationState *s)
{
    return false;
}

bool migration_has_failed(MigrationState *s)
{
    return false;
}

MigMode migrate_mode_of(MigrationState *s)
{
    return 0;
}
