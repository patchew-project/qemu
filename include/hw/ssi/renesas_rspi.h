/*
 * Renesas RX65N Serial Peripheral Interface (RSPI)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Section 30
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_RENESAS_RSPI_H
#define HW_SSI_RENESAS_RSPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_RENESAS_RSPI "renesas-rspi"
typedef struct RX65NRSPIState RX65NRSPIState;
DECLARE_INSTANCE_CHECKER(RX65NRSPIState, RENESAS_RSPI, TYPE_RENESAS_RSPI)

enum {
    RSPI_IRQ_SPEI = 0,  /* error */
    RSPI_IRQ_SPRI = 1,  /* receive buffer full */
    RSPI_IRQ_SPTI = 2,  /* transmit buffer empty */
    RSPI_IRQ_SPII = 3,  /* idle */
    RSPI_NR_IRQ   = 4,
};

struct RX65NRSPIState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    uint64_t input_freq;
    SSIBus *ssi;

    /* Registers */
    uint8_t  spcr;    /* Control */
    uint8_t  sslp;    /* SSL polarity */
    uint8_t  sppcr;   /* Pin control */
    uint8_t  spsr;    /* Status */
    /* Data is double-buffered in hardware; this model uses one buffer. */
    uint32_t spdr;
    uint8_t  spscr;   /* Sequence control */
    uint8_t  spssr;   /* Sequence status */
    uint8_t  spbr;    /* Bit rate */
    uint8_t  spdcr;   /* Data control */
    uint8_t  spckd;   /* Clock delay */
    uint8_t  sslnd;   /* SSL negate delay */
    uint8_t  spnd;    /* Next-access delay */
    uint8_t  spcr2;   /* Control 2 */
    uint16_t spcmd[8]; /* Command registers 0–7 */
    uint8_t  spdcr2;  /* Data control 2 */

    qemu_irq irq[RSPI_NR_IRQ];
};

#endif /* HW_SSI_RENESAS_RSPI_H */
