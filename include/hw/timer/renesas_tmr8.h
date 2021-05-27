/*
 * Renesas 8bit timer Object
 *
 * Copyright (c) 2018 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#ifndef HW_RENESAS_TMR8_H
#define HW_RENESAS_TMR8_H

#include "hw/sysbus.h"

#define TYPE_RENESAS_TMR8 "renesas-tmr8"
OBJECT_DECLARE_TYPE(RenesasTMR8State, RenesasTMR8Class,
                    RENESAS_TMR8)

enum {
    TMR_CH = 2,
};

enum {
    IRQ_CMIA, IRQ_CMIB, IRQ_OVI,
    TMR_NR_IRQ,
};

enum timer_event {
    EVT_NONE, EVT_CMIA, EVT_CMIB, EVT_OVI, EVT_WOVI,
    TMR_NR_EVENTS,
};

enum cor {
    REG_A, REG_B, NR_COR,
};

struct RenesasTMR8State;

struct tmr8_ch {
    uint16_t cnt;
    uint16_t cor[NR_COR];
    uint8_t tcr;
    uint8_t tccr;
    uint8_t tcsr;
    qemu_irq irq[TMR_NR_IRQ];
    QEMUTimer *timer;
    int64_t base;
    int64_t next;
    int64_t clk;
    enum timer_event event;
    int id;
    struct RenesasTMR8State *tmrp;
    bool word;
};

typedef struct RenesasTMR8State {
    SysBusDevice parent_obj;

    uint32_t unit;
    Clock *pck;
    uint64_t input_freq;
    MemoryRegion memory;

    struct tmr8_ch ch[TMR_CH];
} RenesasTMR8State;

#endif
