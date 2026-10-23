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

#define ASCLIN_R_MAX        21
#define ASCLIN_RX_BUF_SIZE  8192

#define TYPE_TRICORE_ASCLIN "tricore_asclin"
OBJECT_DECLARE_SIMPLE_TYPE(TriCoreASCLINState, TRICORE_ASCLIN)

struct TriCoreASCLINState {
    SysBusDevice parent_obj;

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
