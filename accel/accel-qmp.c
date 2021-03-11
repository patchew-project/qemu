/*
 * QEMU accelerators, QMP commands
 *
 * Copyright (c) 2021 Red Hat Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-machine.h"

static const Accelerator accel_list[] = {
    ACCELERATOR_QTEST,
#ifdef CONFIG_TCG
    ACCELERATOR_TCG,
#endif
#ifdef CONFIG_KVM
    ACCELERATOR_KVM,
#endif
#ifdef CONFIG_HAX
    ACCELERATOR_HAX,
#endif
#ifdef CONFIG_HVF
    ACCELERATOR_HVF,
#endif
#ifdef CONFIG_WHPX
    ACCELERATOR_WHPX,
#endif
#ifdef CONFIG_XEN_BACKEND
    ACCELERATOR_XEN,
#endif
};

AcceleratorInfoList *qmp_query_accels(Error **errp)
{
    AcceleratorInfoList *list = NULL, **tail = &list;

    for (unsigned i = 0; i < ARRAY_SIZE(accel_list); i++) {
        AcceleratorInfo *info = g_new0(AcceleratorInfo, 1);

        info->name = accel_list[i];

        QAPI_LIST_APPEND(tail, info);
    }

    return list;
}
