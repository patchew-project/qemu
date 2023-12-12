/*
 * Cortex-MPCore internal peripheral emulation.
 *
 * Copyright (c) 2023 Linaro Limited.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/cpu/cortex_mpcore.h"

static const TypeInfo cortex_mpcore_types[] = {
    {
        .name           = TYPE_CORTEX_MPCORE_PRIV,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(CortexMPPrivState),
        .class_size     = sizeof(CortexMPPrivClass),
        .abstract       = true,
    },
};

DEFINE_TYPES(cortex_mpcore_types)
