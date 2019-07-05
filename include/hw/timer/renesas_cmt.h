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

enum {
    CMT_CH = 2,
    CMT_NR_IRQ = 1 * CMT_CH,
};

typedef struct RCMTState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    uint16_t cmstr;
    uint16_t cmcr[CMT_CH];
    uint16_t cmcnt[CMT_CH];
    uint16_t cmcor[CMT_CH];
    int64_t tick[CMT_CH];
    qemu_irq cmi[CMT_CH];
    QEMUTimer *timer[CMT_CH];
} RCMTState;

#endif
