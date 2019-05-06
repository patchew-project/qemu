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

enum timer_event {cmia = 0,
                  cmib = 1,
                  ovi = 2,
                  none = 3,
                  TMR_NR_EVENTS = 4};
enum {CH = 2};
typedef struct RTMRState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    MemoryRegion memory;

    uint8_t tcnt[CH];
    uint8_t tcora[CH];
    uint8_t tcorb[CH];
    uint8_t tcr[CH];
    uint8_t tccr[CH];
    uint8_t tcor[CH];
    uint8_t tcsr[CH];
    int64_t tick;
    int64_t div_round[CH];
    enum timer_event next[CH];
    qemu_irq cmia[CH];
    qemu_irq cmib[CH];
    qemu_irq ovi[CH];
    QEMUTimer *timer[CH];
} RTMRState;

#endif
