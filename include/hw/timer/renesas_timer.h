/*
 * Renesas Timer unit Object
 *
 * Copyright (c) 2021 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#ifndef HW_RENESAS_TIMER_H
#define HW_RENESAS_TIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"

#define TYPE_RENESAS_TIMER_BASE "renesas-timer"
OBJECT_DECLARE_TYPE(RenesasTimerBaseState, RenesasTimerBaseClass,
                    RENESAS_TIMER_BASE)
#define TYPE_RENESAS_CMT "renesas-cmt"
OBJECT_DECLARE_TYPE(RenesasCMTState, RenesasCMTClass,
                    RENESAS_CMT)
#define TYPE_RENESAS_TMU "renesas-tmu"
OBJECT_DECLARE_TYPE(RenesasTMUState, RenesasTMUClass,
                    RENESAS_TMU)

enum {
    TIMER_CH_CMT = 2,
    TIMER_CH_TMU = 3,
};

enum {
    CMT_NR_IRQ = 1 * TIMER_CH_CMT,
};

enum {
    TIMER_START = 1,
    TIMER_STOP = 0,
};

struct RenesasTimerBaseState;

struct rtimer_ch {
    uint16_t ctrl;
    qemu_irq irq;
    ptimer_state *timer;
    bool start;
    struct RenesasTimerBaseState *tmrp;
};

typedef struct RenesasTimerBaseState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    struct rtimer_ch ch[TIMER_CH_TMU];
    int num_ch;
    int unit;
} RenesasTimerBaseState;

typedef struct RenesasCMTState {
    RenesasTimerBaseState parent_obj;
} RenesasCMTState;

typedef struct RenesasTMUState {
    RenesasTimerBaseState parent_obj;
    uint8_t tocr;
    MemoryRegion memory_p4;
    MemoryRegion memory_a7;
} RenesasTMUState;

typedef struct RenesasTimerBaseClass {
    SysBusDeviceClass parent;
    int (*divrate)(RenesasTimerBaseState *tmr, int ch);
    void (*timer_event)(void *opaque);
    int64_t (*convert_count)(int64_t val, ptimer_state *t);
    void (*update_clk)(RenesasTimerBaseState *tmr, int ch);
} RenesasTimerBaseClass;

typedef struct RenesasCMTClass {
    RenesasTimerBaseClass parent;
} RenesasCMTClass;

typedef struct RenesasTMUClass {
    RenesasTimerBaseClass parent;
    void (*p_update_clk)(RenesasTimerBaseState *tmr, int ch);
} RenesasTMUClass;

#endif
