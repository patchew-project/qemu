/*
 * Renesas Compare-match timer Object
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#ifndef HW_RENESAS_CMT_H
#define HW_RENESAS_CMT_H

#include "hw/sysbus.h"

#define TYPE_RENESAS_CMT "renesas-cmt"
#define RCMT(obj) OBJECT_CHECK(RCMTState, (obj), TYPE_RENESAS_CMT)

typedef struct RCMTState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    uint16_t cmstr;
    uint16_t cmcr[2];
    uint16_t cmcnt[2];
    uint16_t cmcor[2];
    int64_t tick[2];
    qemu_irq cmi[2];
    QEMUTimer *timer[2];
} RCMTState;

#endif
