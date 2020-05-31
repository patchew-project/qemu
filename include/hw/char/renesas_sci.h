/*
 * Renesas Serial Communication Interface
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qemu/fifo8.h"
#include "hw/sysbus.h"

#define TYPE_RENESAS_SCI "renesas-sci"
#define RSCI(obj) OBJECT_CHECK(RSCIState, (obj), TYPE_RENESAS_SCI)

enum {
    ERI = 0,
    RXI = 1,
    TXI = 2,
    TEI = 3,
    BRI = 3,
    SCI_NR_IRQ = 4,
};

enum {
    SCI_FEAT_SCI = 0x00,
    SCI_FEAT_SCIA = 0x01,
    SCI_FEAT_SCIF = 0x10,
};

enum {
    RXTOUT,
    RXNEXT,
    TXEMPTY,
    TXEND,
    NR_SCI_EVENT,
};

typedef struct RSCIState {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    MemoryRegion memory_p4;
    MemoryRegion memory_a7;

    /* SCI register */
    uint8_t smr;
    uint8_t brr;
    uint8_t scr;
    uint8_t tdr;
    uint16_t Xsr;
    uint8_t scmr;
    uint8_t semr;
    uint16_t fcr;
    uint16_t sptr;
    uint16_t lsr;

    /* internal use */
    uint16_t read_Xsr;
    uint16_t read_lsr;
    int64_t etu;
    int64_t trtime;
    int64_t tx_start_time;
    int tdcnt;
    int regsize;
    struct {
        int64_t time;
        int64_t (*handler)(struct RSCIState *sci);
    } event[NR_SCI_EVENT];
    QEMUTimer *event_timer;
    CharBackend chr;
    uint64_t input_freq;
    int feature;
    qemu_irq irq[SCI_NR_IRQ];
    Fifo8 rxfifo;
} RSCIState;
