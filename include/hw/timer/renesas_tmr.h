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

#define TYPE_RENESAS_TMR "renesas-tmr"
#define RTMR(obj) OBJECT_CHECK(RTMRState, (obj), TYPE_RENESAS_TMR)

enum timer_event {none, cmia, cmib, ovi};

typedef struct RTMRState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    uint16_t tcnt[2];
    uint8_t tcora[2];
    uint8_t tcorb[2];
    uint8_t tcr[2];
    uint8_t tccr[2];
    uint8_t tcor[2];
    uint8_t tcsr[2];
    int64_t tick;
    int64_t div_round[2];
    enum timer_event next[2];
    qemu_irq cmia[2];
    qemu_irq cmib[2];
    qemu_irq ovi[2];
    QEMUTimer *timer[2];
} RTMRState;

#endif
