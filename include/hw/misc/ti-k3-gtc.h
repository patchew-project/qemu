/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 */
#ifndef HW_MISC_TI_K3_GTC_H
#define HW_MISC_TI_K3_GTC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_GTC "ti-k3-gtc"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3GtcState, TI_K3_GTC)

struct TIK3GtcState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t cntcr;    /* GTC control: bit0 EN. Read back enabled. */
    uint32_t cntfid0;  /* GTC frequency id 0, in Hz. */
};

#endif /* HW_MISC_TI_K3_GTC_H */
