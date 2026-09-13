/*
 * Renesas RX Data Transfer Controller (DTC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 18 (DTC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_DMA_RENESAS_RX_DTC_H
#define HW_DMA_RENESAS_RX_DTC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_RX_DTC "renesas-rx-dtc"
typedef struct RenesasRxDtcState RenesasRxDtcState;
DECLARE_INSTANCE_CHECKER(RenesasRxDtcState, RENESAS_RX_DTC, TYPE_RENESAS_RX_DTC)

#define RX_DTC_REGS_SIZE    0x20

struct RenesasRxDtcState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mr;

    uint8_t  dtccr;     /* control                    */
    uint32_t dtcvbr;    /* vector base address        */
    uint8_t  dtcadmod;  /* address mode               */
    uint8_t  dtcst;     /* module start               */
    uint16_t dtcsts;    /* status                     */
    uint32_t dtcibr;    /* index table base           */
    uint8_t  dtcor;     /* operation                  */
    uint16_t dtcexbr;   /* extended repeat-area base  */
};

#endif /* HW_DMA_RENESAS_RX_DTC_H */
