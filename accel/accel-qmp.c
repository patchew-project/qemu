/*
 * QEMU accelerators, QMP commands
 *
 * Copyright (c) 2021 Red Hat Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qapi/qapi-types-machine-target.h"
#include "qapi/qapi-commands-machine-target.h"

AcceleratorInfoList *qmp_query_accels(Error **errp)
{
    AcceleratorInfoList *list = NULL, **tail = &list;

    for (Accelerator accel = 0; accel < ACCELERATOR__MAX; accel++) {
        AcceleratorInfo *info;

        if (!accel_find(Accelerator_str(accel))) {
            /* Accelerator available at build time but not at runtime. */
            continue;
        }

        info = g_new0(AcceleratorInfo, 1);
        info->name = accel;
        QAPI_LIST_APPEND(tail, info);
    }

    return list;
}
