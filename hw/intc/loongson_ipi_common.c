/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson ipi interrupt common support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/intc/loongson_ipi_common.h"

static const TypeInfo loongson_ipi_common_info = {
    .name           = TYPE_LOONGSON_IPI_COMMON,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(LoongsonIPICommonState),
    .class_size     = sizeof(LoongsonIPICommonClass),
    .abstract       = true,
};

static void loongson_ipi_common_register_types(void)
{
    type_register_static(&loongson_ipi_common_info);
}

type_init(loongson_ipi_common_register_types)
