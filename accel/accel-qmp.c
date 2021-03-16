/*
 * QEMU accelerators, QMP commands
 *
 * Copyright (c) 2021 Red Hat Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-machine.h"

static const bool accel_builtin_list[ACCELERATOR__MAX] = {
    [ACCELERATOR_QTEST] = true,
#ifdef CONFIG_TCG
    [ACCELERATOR_TCG] = true,
#endif
#ifdef CONFIG_KVM
    [ACCELERATOR_KVM] = true,
#endif
#ifdef CONFIG_HAX
    [ACCELERATOR_HAX] = true,
#endif
#ifdef CONFIG_HVF
    [ACCELERATOR_HVF] = true,
#endif
#ifdef CONFIG_WHPX
    [ACCELERATOR_WHPX] = true,
#endif
#ifdef CONFIG_XEN_BACKEND
    [ACCELERATOR_XEN] = true,
#endif
};

AcceleratorInfoList *qmp_query_accels(Error **errp)
{
    AcceleratorInfoList *list = NULL, **tail = &list;

    for (Accelerator accel = 0; accel < ACCELERATOR__MAX; accel++) {
        if (accel_builtin_list[accel]) {
            AcceleratorInfo *info = g_new0(AcceleratorInfo, 1);

            info->name = accel;

            QAPI_LIST_APPEND(tail, info);
        }
    }

    return list;
}
