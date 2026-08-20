/*
 * TI K3 DM timer (am654 layout)
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_TIMER_TI_K3_DMTIMER_H
#define HW_TIMER_TI_K3_DMTIMER_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_DMTIMER "ti-k3-dmtimer"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3DmTimerState, TI_K3_DMTIMER)

struct TIK3DmTimerState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t freq_hz;
    uint32_t tclr;
    uint32_t tldr;
    /* counter value latched at last write/start */
    uint32_t tcrr_base;
    int64_t base_ns;
    bool running;
};

#endif
