/*
 * Copyright 2016,2017 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _INTC_XIVE_INTERNAL_H
#define _INTC_XIVE_INTERNAL_H

#include <hw/sysbus.h>

/* Utilities to manipulate these (originaly from OPAL) */
#define MASK_TO_LSH(m)          (__builtin_ffsl(m) - 1)
#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val)                             \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

#define PPC_BIT(bit)            (0x8000000000000000UL >> (bit))
#define PPC_BIT32(bit)          (0x80000000UL >> (bit))
#define PPC_BIT8(bit)           (0x80UL >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK32(bs, be)   ((PPC_BIT32(bs) - PPC_BIT32(be)) | \
                                 PPC_BIT32(bs))

/* IVE/EAS
 *
 * One per interrupt source. Targets that interrupt to a given EQ
 * and provides the corresponding logical interrupt number (EQ data)
 *
 * We also map this structure to the escalation descriptor inside
 * an EQ, though in that case the valid and masked bits are not used.
 */
typedef struct XiveIVE {
        /* Use a single 64-bit definition to make it easier to
         * perform atomic updates
         */
        uint64_t        w;
#define IVE_VALID       PPC_BIT(0)
#define IVE_EQ_BLOCK    PPC_BITMASK(4, 7)        /* Destination EQ block# */
#define IVE_EQ_INDEX    PPC_BITMASK(8, 31)       /* Destination EQ index */
#define IVE_MASKED      PPC_BIT(32)              /* Masked */
#define IVE_EQ_DATA     PPC_BITMASK(33, 63)      /* Data written to the EQ */
} XiveIVE;

/* EQ */
typedef struct XiveEQ {
        uint32_t        w0;
#define EQ_W0_VALID             PPC_BIT32(0)
#define EQ_W0_ENQUEUE           PPC_BIT32(1)
#define EQ_W0_UCOND_NOTIFY      PPC_BIT32(2)
#define EQ_W0_BACKLOG           PPC_BIT32(3)
#define EQ_W0_PRECL_ESC_CTL     PPC_BIT32(4)
#define EQ_W0_ESCALATE_CTL      PPC_BIT32(5)
#define EQ_W0_END_OF_INTR       PPC_BIT32(6)
#define EQ_W0_QSIZE             PPC_BITMASK32(12, 15)
#define EQ_W0_SW0               PPC_BIT32(16)
#define EQ_W0_FIRMWARE          EQ_W0_SW0 /* Owned by FW */
#define EQ_QSIZE_4K             0
#define EQ_QSIZE_64K            4
#define EQ_W0_HWDEP             PPC_BITMASK32(24, 31)
        uint32_t        w1;
#define EQ_W1_ESn               PPC_BITMASK32(0, 1)
#define EQ_W1_ESn_P             PPC_BIT32(0)
#define EQ_W1_ESn_Q             PPC_BIT32(1)
#define EQ_W1_ESe               PPC_BITMASK32(2, 3)
#define EQ_W1_ESe_P             PPC_BIT32(2)
#define EQ_W1_ESe_Q             PPC_BIT32(3)
#define EQ_W1_GENERATION        PPC_BIT32(9)
#define EQ_W1_PAGE_OFF          PPC_BITMASK32(10, 31)
        uint32_t        w2;
#define EQ_W2_MIGRATION_REG     PPC_BITMASK32(0, 3)
#define EQ_W2_OP_DESC_HI        PPC_BITMASK32(4, 31)
        uint32_t        w3;
#define EQ_W3_OP_DESC_LO        PPC_BITMASK32(0, 31)
        uint32_t        w4;
#define EQ_W4_ESC_EQ_BLOCK      PPC_BITMASK32(4, 7)
#define EQ_W4_ESC_EQ_INDEX      PPC_BITMASK32(8, 31)
        uint32_t        w5;
#define EQ_W5_ESC_EQ_DATA       PPC_BITMASK32(1, 31)
        uint32_t        w6;
#define EQ_W6_FORMAT_BIT        PPC_BIT32(8)
#define EQ_W6_NVT_BLOCK         PPC_BITMASK32(9, 12)
#define EQ_W6_NVT_INDEX         PPC_BITMASK32(13, 31)
        uint32_t        w7;
#define EQ_W7_F0_IGNORE         PPC_BIT32(0)
#define EQ_W7_F0_BLK_GROUPING   PPC_BIT32(1)
#define EQ_W7_F0_PRIORITY       PPC_BITMASK32(8, 15)
#define EQ_W7_F1_WAKEZ          PPC_BIT32(0)
#define EQ_W7_F1_LOG_SERVER_ID  PPC_BITMASK32(1, 31)
} XiveEQ;

#define XIVE_EQ_PRIORITY_COUNT 8
#define XIVE_PRIORITY_MAX  (XIVE_EQ_PRIORITY_COUNT - 1)

struct XIVE {
    SysBusDevice parent;

    /* Properties */
    uint32_t     chip_id;
    uint32_t     nr_targets;

    /* IRQ number allocator */
    uint32_t     int_count;     /* Number of interrupts: nr_targets + HW IRQs */
    uint32_t     int_base;      /* Min index */
    uint32_t     int_max;       /* Max index */
    uint32_t     int_hw_bot;    /* Bottom index of HW IRQ allocator */
    uint32_t     int_ipi_top;   /* Highest IPI index handed out so far + 1 */

    /* XIVE internal tables */
    void         *sbe;
    XiveIVE      *ivt;
    XiveEQ       *eqdt;

    /* ESB and TIMA memory location */
    hwaddr       vc_base;
    MemoryRegion esb_iomem;
};

void xive_reset(void *dev);
XiveIVE *xive_get_ive(XIVE *x, uint32_t isn);
XiveEQ *xive_get_eq(XIVE *x, uint32_t idx);

bool xive_eq_for_target(XIVE *x, uint32_t target, uint8_t prio,
                        uint32_t *out_eq_idx);

#endif /* _INTC_XIVE_INTERNAL_H */
