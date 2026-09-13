/*
 * Renesas RX CRC Calculator (CRCA)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RENESAS_RX_CRC_H
#define HW_MISC_RENESAS_RX_CRC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_RX_CRC "renesas-rx-crc"
OBJECT_DECLARE_SIMPLE_TYPE(RenesasRxCrcState, RENESAS_RX_CRC)

/* CRCCR, CRCDIR and CRCDOR occupy 0x8280, 0x8284 and 0x8288. */
#define RX_CRC_SIZE     0x10

struct RenesasRxCrcState {
    SysBusDevice parent_obj;

    MemoryRegion memory;

    uint8_t crccr;      /* control: GPS[2:0], LMS, DORCLR */
    uint32_t crcdor;    /* running result, also the initial value */
};

#endif /* HW_MISC_RENESAS_RX_CRC_H */
