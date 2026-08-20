/*
 * TI K3 CTRL_MMR stub
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_CTRLMMR_H
#define HW_MISC_TI_K3_CTRLMMR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_CTRLMMR "ti-k3-ctrlmmr"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3CtrlMmrState, TI_K3_CTRLMMR)

struct TIK3CtrlMmrState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t devstat;
    uint32_t rst_src;
    uint32_t sec_mgr_sys_status;
};

#endif
