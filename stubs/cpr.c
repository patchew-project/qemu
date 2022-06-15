/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/cpr.h"

int cpr_add_blocker(Error **reasonp, Error **errp, CprMode mode, ...)
{
    return 0;
}

int cpr_add_blocker_str(const char *reason, Error **errp, CprMode mode, ...)
{
    return 0;
}

void cpr_del_blocker(Error **reasonp)
{
}

void cpr_add_notifier(Notifier *notify,
                      void (*cb)(Notifier *notifier, void *data),
                      CprNotifyState state)
{
}

void cpr_remove_notifier(Notifier *notify)
{
}
