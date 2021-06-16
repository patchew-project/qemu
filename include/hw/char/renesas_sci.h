/*
 * Renesas Serial Communication Interface
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_RENESAS_SCI_H
#define HW_CHAR_RENESAS_SCI_H

#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qemu/fifo8.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_SCI_BASE "renesas-sci-base"
#define TYPE_RENESAS_SCIA "renesas-scia"

OBJECT_DECLARE_TYPE(RenesasSCIBaseState, RenesasSCIBaseClass,
                    RENESAS_SCI_BASE)
OBJECT_DECLARE_TYPE(RenesasSCIAState, RenesasSCIAClass,
                    RENESAS_SCIA)

enum {
    ERI = 0,
    RXI = 1,
    TXI = 2,
    BRI_TEI = 3,
    SCI_NR_IRQ = 4,
};

enum {
    RXNEXT,
    TXEMPTY,
    TXEND,
    NR_SCI_EVENT,
};

enum {
    SCI_REGWIDTH_8 = 8,
    SCI_REGWIDTH_16 = 16,
    SCI_REGWIDTH_32 = 32,
};

typedef struct RenesasSCIBaseState {
    /*< private >*/
    SysBusDevice parent_obj;

    MemoryRegion memory;
    QEMUTimer *event_timer;

    /*< public >*/
    uint64_t input_freq;
    int64_t etu;
    int64_t trtime;
    int64_t tx_start_time;
    Fifo8 rxfifo;
    int regshift;
    uint32_t unit;
    CharBackend chr;
    qemu_irq irq[SCI_NR_IRQ];
    struct {
        int64_t time;
        int64_t (*handler)(struct RenesasSCIBaseState *sci);
    } event[NR_SCI_EVENT];

    /* common SCI register */
    uint8_t smr;
    uint8_t brr;
    uint8_t scr;
    uint8_t tdr;
    uint16_t Xsr;
} RenesasSCIBaseState;

struct RenesasSCIAState {
    RenesasSCIBaseState parent_obj;

    /* SCIa specific register */
    uint8_t scmr;
    uint8_t semr;
};

typedef struct RenesasSCIBaseClass {
    SysBusDeviceClass parent;

    const struct MemoryRegionOps *ops;
    void (*irq_fn)(struct RenesasSCIBaseState *sci, int request);
    int (*divrate)(struct RenesasSCIBaseState *sci);
} RenesasSCIBaseClass;

typedef struct RenesasSCIAClass {
    RenesasSCIBaseClass parent;

    void (*p_irq_fn)(struct RenesasSCIBaseState *sci, int request);
    int (*p_divrate)(struct RenesasSCIBaseState *sci);
} RenesasSCIAClass;

#endif
