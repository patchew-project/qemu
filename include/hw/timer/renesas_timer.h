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
#include "hw/qdev-clock.h"

#define TYPE_RENESAS_TIMER_BASE "renesas-timer"
#define RenesasTimerBase(obj) \
    OBJECT_CHECK(RenesasTimerBaseState, (obj), TYPE_RENESAS_TIMER_BASE)
#define TYPE_RENESAS_CMT "renesas-cmt"
#define RenesasCMT(obj) OBJECT_CHECK(RenesasCMTState, (obj), TYPE_RENESAS_CMT)
#define TYPE_RENESAS_TMU "renesas-tmu"
#define RenesasTMU(obj) OBJECT_CHECK(RenesasTMUState, (obj), TYPE_RENESAS_TMU)

#define RenesasTimer_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RenesasTimerBaseClass, obj, TYPE_RENESAS_TIMER_BASE)
#define TimerBaseClass(klass) \
    OBJECT_CLASS_CHECK(RenesasTimerBaseClass, klass, TYPE_RENESAS_TIMER_BASE)
#define CMTClass(klass) \
    OBJECT_CLASS_CHECK(RenesasCMTClass, klass, TYPE_RENESAS_CMT)
#define TMUClass(klass) \
    OBJECT_CLASS_CHECK(RenesasTMUClass, klass, TYPE_RENESAS_TMU)

enum {
    TIMER_CH_CMT = 2,
    TIMER_CH_TMU = 3,
};

enum {
    CMT_NR_IRQ = 1 * TIMER_CH_CMT,
};

struct RTIMERState;

enum dirction {
    countup, countdown,
};

struct rtimer_ch {
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

typedef struct RenesasTimerBaseState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;
    MemoryRegion memory_p4;
    MemoryRegion memory_a7;
    Clock *pck;

    struct rtimer_ch ch[TIMER_CH_TMU];
    int num_ch;
    enum dirction direction;
    int unit;
} RenesasTimerBaseState;

typedef struct RenesasCMTState {
    RenesasTimerBaseState parent_obj;
} RenesasCMTState;

typedef struct RenesasTMUState {
    RenesasTimerBaseState parent_obj;
    uint8_t tocr;
} RenesasTMUState;

typedef struct RenesasTimerBaseClass {
    SysBusDeviceClass parent;
    int (*divrate)(RenesasTimerBaseState *tmr, int ch);
    void (*timer_event)(void *opaque);
    int64_t (*delta_to_tcnt)(RenesasTimerBaseState *tmr, int ch, int64_t delta);
    int64_t (*get_next)(RenesasTimerBaseState *tmr, int ch);
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
