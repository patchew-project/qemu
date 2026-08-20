/*
 * TI K3 DDRSS register-file stub (AM64x)
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_DDRSS_H
#define HW_MISC_TI_K3_DDRSS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_DDRSS "ti-k3-ddrss"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3DdrssState, TI_K3_DDRSS)

#define TI_K3_DDRSS_CFG_SIZE 0x8000

struct TIK3DdrssState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[TI_K3_DDRSS_CFG_SIZE / 4];
};

#endif
