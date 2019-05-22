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
};

struct RCMTChannelState {
    uint16_t cmcr;
    uint16_t cmcnt;
    uint16_t cmcor;

    bool start;
    int64_t tick;
    int64_t clk_per_ns;
    qemu_irq cmi;
    QEMUTimer *timer;
};

typedef struct RCMTState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    uint16_t cmstr;
    struct RCMTChannelState c[CMT_CH];
} RCMTState;

#endif
