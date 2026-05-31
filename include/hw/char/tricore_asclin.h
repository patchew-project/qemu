/*
 * TriCore ASCLIN (Asynchronous/Synchronous Interface) UART controller
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2024 Siemens AG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TRICORE_ASCLIN_H
#define HW_TRICORE_ASCLIN_H

#include "chardev/char-fe.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

/* FLAGS register bits */
#define ASCLIN_FLAGS_TH     (1u << 0)
#define ASCLIN_FLAGS_TR     (1u << 1)
#define ASCLIN_FLAGS_RH     (1u << 2)
#define ASCLIN_FLAGS_RR     (1u << 3)
#define ASCLIN_FLAGS_FED    (1u << 5)
#define ASCLIN_FLAGS_RED    (1u << 6)
#define ASCLIN_FLAGS_PE     (1u << 16)
#define ASCLIN_FLAGS_TC     (1u << 17)
#define ASCLIN_FLAGS_FE     (1u << 18)
#define ASCLIN_FLAGS_HT     (1u << 19)
#define ASCLIN_FLAGS_RT     (1u << 20)
#define ASCLIN_FLAGS_BD     (1u << 21)
#define ASCLIN_FLAGS_LP     (1u << 22)
#define ASCLIN_FLAGS_LA     (1u << 23)
#define ASCLIN_FLAGS_LC     (1u << 24)
#define ASCLIN_FLAGS_CE     (1u << 25)
#define ASCLIN_FLAGS_RFO    (1u << 26)
#define ASCLIN_FLAGS_RFU    (1u << 27)
#define ASCLIN_FLAGS_RFL    (1u << 28)
#define ASCLIN_FLAGS_TFO    (1u << 30)
#define ASCLIN_FLAGS_TFL    (1u << 31)

/* RXFIFOCON bits */
#define ASCLIN_RXFIFOCON_FLUSH  0x1
#define ASCLIN_RXFIFOCON_ENI    0x2

/* Interrupt line grouping masks */
#define ASCLIN_TX_INT_MASK  (ASCLIN_FLAGS_TH | ASCLIN_FLAGS_TR | \
                             ASCLIN_FLAGS_TFL)
#define ASCLIN_RX_INT_MASK  (ASCLIN_FLAGS_RH | ASCLIN_FLAGS_RR | \
                             ASCLIN_FLAGS_RFL)
#define ASCLIN_ERR_INT_MASK (ASCLIN_FLAGS_FED | ASCLIN_FLAGS_RED | \
                             ASCLIN_FLAGS_PE  | ASCLIN_FLAGS_TC  | \
                             ASCLIN_FLAGS_FE  | ASCLIN_FLAGS_HT  | \
                             ASCLIN_FLAGS_RT  | ASCLIN_FLAGS_BD  | \
                             ASCLIN_FLAGS_LP  | ASCLIN_FLAGS_LA  | \
                             ASCLIN_FLAGS_LC  | ASCLIN_FLAGS_CE  | \
                             ASCLIN_FLAGS_RFO | ASCLIN_FLAGS_RFU | \
                             ASCLIN_FLAGS_TFO)

/* FIFO depth and FILL field position in TX/RXFIFOCON */
#define ASCLIN_HW_FIFO_DEPTH    16
#define ASCLIN_FILL_SHIFT       16
#define ASCLIN_FILL_MASK        (0x1fu << ASCLIN_FILL_SHIFT)

#define ASCLIN_R_MAX        21
#define ASCLIN_RX_BUF_SIZE  8192

#define TYPE_TRICORE_ASCLIN "tricore_asclin"
OBJECT_DECLARE_SIMPLE_TYPE(TriCoreASCLINState, TRICORE_ASCLIN)

struct TriCoreASCLINState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    CharFrontend chr;
    qemu_irq irq_rx;
    qemu_irq irq_tx;
    qemu_irq irq_err;

    guint watch_tag;
    uint32_t regs[ASCLIN_R_MAX];
    uint32_t txbuf;

    uint8_t rxbuf[ASCLIN_RX_BUF_SIZE];
    uint32_t rx_wridx;
    uint32_t rx_rdidx;
};

#endif
