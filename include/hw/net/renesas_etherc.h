/*
 * Renesas RX65N Ethernet Controller (ETHERC) + Ethernet DMA Controller (EDMAC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Sections 32–33
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_RENESAS_ETHERC_H
#define HW_NET_RENESAS_ETHERC_H

#include "hw/core/sysbus.h"
#include "net/net.h"
#include "qom/object.h"

#define TYPE_RENESAS_ETHERC "renesas-etherc"
typedef struct RX65NEthercState RX65NEthercState;
DECLARE_INSTANCE_CHECKER(RX65NEthercState, RENESAS_ETHERC, TYPE_RENESAS_ETHERC)

/* MMIO covers ETHERC (0x000–0x1FF) + EDMAC (0x200–0x3FF) */
#define ETHERC_MMIO_SIZE    0x200

enum {
    ETHERC_IRQ_EINT = 0,  /* EDMAC interrupt (vector 32) */
    ETHERC_NR_IRQ   = 1,
};

/* MDIO bit-bang state machine */
typedef struct {
    int     preamble;       /* consecutive MDO=1 count */
    int     in_cnt;         /* master bits received (-1 = idle) */
    uint32_t sr;            /* shift register for header bits */
    bool    is_read;        /* current transaction is a read */
    int     write_cnt;      /* write data/TA bits accumulated */
    int     write_paddr;
    int     write_raddr;
    uint16_t write_data;
    uint16_t read_data;     /* latched PHY register value */
    int     out_cnt;        /* slave output bit counter (-1 = idle) */
    bool    mdi;            /* current MDI output level */
    uint16_t phy_regs[32];  /* shared register bank for PHY addresses 0 and 1 */
} MDIOState;

struct RX65NEthercState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    NICState *nic;
    NICConf conf;

    qemu_irq irq[ETHERC_NR_IRQ];

    /* ETHERC registers (offsets 0x000–0x0FF) */
    uint32_t ecmr;
    uint32_t rflr;
    uint32_t ecsr;
    uint32_t ecsipr;
    uint32_t pir;
    uint32_t rdmlr;
    uint32_t ipgr;
    uint32_t apr;
    uint32_t mpr;
    uint32_t tpauser;
    uint32_t bcfrr;
    uint32_t mahr;
    uint32_t malr;
    uint32_t trocr;
    uint32_t cdcr;
    uint32_t lccr;
    uint32_t cndcr;
    uint32_t cefcr;
    uint32_t frecr;
    uint32_t tsfrcr;
    uint32_t tlfrcr;
    uint32_t rfcr;
    uint32_t mafcr;

    /* EDMAC registers (offsets 0x200–0x2FF) */
    uint32_t edmr;
    uint32_t edtrr;
    uint32_t edrrr;
    uint32_t tdlar;
    uint32_t rdlar;
    uint32_t eesr;
    uint32_t eesipr;
    uint32_t trscer;
    uint32_t rmfcr;
    uint32_t tftr;
    uint32_t fdr;
    uint32_t rmcr;
    uint32_t tfucr;
    uint32_t rfocr;
    uint32_t fcftr;
    uint32_t rpadir;
    uint32_t trimd;

    /* DMA ring state */
    uint32_t tx_cur;
    uint32_t rx_cur;

    MDIOState mdio;
};

#endif /* HW_NET_RENESAS_ETHERC_H */
