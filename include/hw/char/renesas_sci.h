/*
 * Renesas Serial Communication Interface
 *
 * Copyright (c) 2018 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"

#define TYPE_RENESAS_SCI "renesas-sci"
#define RSCI(obj) OBJECT_CHECK(RSCIState, (obj), TYPE_RENESAS_SCI)

#define ERI 0
#define RXI 1
#define TXI 2
#define TEI 3

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion memory;

    uint8_t smr;
    uint8_t brr;
    uint8_t scr;
    uint8_t tdr;
    uint8_t ssr;
    uint8_t rdr;
    uint8_t scmr;
    uint8_t semr;

    uint8_t read_ssr;
    long long trtime;
    long long rx_next;
    QEMUTimer *timer;
    CharBackend chr;
    uint64_t input_freq;
    qemu_irq irq[4];
} RSCIState;
