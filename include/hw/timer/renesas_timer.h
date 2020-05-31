/*
 * Renesas Timer unit Object
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#ifndef HW_RENESAS_TIMER_H
#define HW_RENESAS_TIMER_H

#include "hw/sysbus.h"

#define TYPE_RENESAS_TIMER "renesas-timer"
#define RTIMER(obj) OBJECT_CHECK(RTIMERState, (obj), TYPE_RENESAS_TIMER)

enum {
    TIMER_CH_CMT = 2,
    /* TMU have 5channels. It separated 0-2 and 3-4. */
    TIMER_CH_TMU = 3,
};

enum {
    RTIMER_FEAT_CMT,
    RTIMER_FEAT_TMU_LOW,
    RTIMER_FEAT_TMU_HIGH,
};

struct RTIMERState;

struct channel_rtimer {
    uint32_t cnt;
    uint32_t cor;
    uint16_t ctrl;
    qemu_irq irq;
    int64_t base;
    int64_t next;
    uint64_t clk;
    bool start;
    QEMUTimer *timer;
    struct RTIMERState *tmrp;
};

typedef struct RTIMERState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;
    MemoryRegion memory_p4;
    MemoryRegion memory_a7;

    uint8_t tocr;
    struct channel_rtimer ch[TIMER_CH_TMU];
    uint32_t feature;
    int num_ch;
} RTIMERState;

#endif
