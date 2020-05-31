/*
 * Renesas 8bit timer Object
 *
 * Copyright (c) 2018 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#ifndef HW_RENESAS_TMR_H
#define HW_RENESAS_TMR_H

#include "hw/sysbus.h"

#define TYPE_RENESAS_8TMR "renesas-8tmr"
#define RTMR(obj) OBJECT_CHECK(RTMRState, (obj), TYPE_RENESAS_8TMR)

enum timer_event {
    cmia, cmib, ovi, wovi,
    TMR_NR_EVENTS,
};

enum {
    TMR_CH = 2,
    TMR_NR_COR = 2,
    TMR_NR_IRQ = 3,
};

enum {
    IRQ_CMIA, IRQ_CMIB, IRQ_OVI,
};

struct RTMRState;

struct channel_8tmr {
    uint16_t cnt;
    uint16_t cor[TMR_NR_COR];
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
    struct RTMRState *tmrp;
    bool word;
};

typedef struct RTMRState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    struct channel_8tmr ch[TMR_CH];
} RTMRState;

#endif
