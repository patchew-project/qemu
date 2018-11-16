/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2016-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_XIVE_REGS_H
#define PPC_XIVE_REGS_H

/* EAS (Event Assignment Structure)
 *
 * One per interrupt source. Targets an interrupt to a given Event
 * Notification Descriptor (END) and provides the corresponding
 * logical interrupt number (END data)
 */
typedef struct XiveEAS {
        /* Use a single 64-bit definition to make it easier to
         * perform atomic updates
         */
        uint64_t        w;
#define EAS_VALID       PPC_BIT(0)
#define EAS_END_BLOCK   PPC_BITMASK(4, 7)        /* Destination END block# */
#define EAS_END_INDEX   PPC_BITMASK(8, 31)       /* Destination END index */
#define EAS_MASKED      PPC_BIT(32)              /* Masked */
#define EAS_END_DATA    PPC_BITMASK(33, 63)      /* Data written to the END */
} XiveEAS;

/* Event Notification Descriptor (END) */
typedef struct XiveEND {
        uint32_t        w0;
#define END_W0_VALID             PPC_BIT32(0) /* "v" bit */
#define END_W0_ENQUEUE           PPC_BIT32(1) /* "q" bit */
#define END_W0_UCOND_NOTIFY      PPC_BIT32(2) /* "n" bit */
#define END_W0_BACKLOG           PPC_BIT32(3) /* "b" bit */
#define END_W0_PRECL_ESC_CTL     PPC_BIT32(4) /* "p" bit */
#define END_W0_ESCALATE_CTL      PPC_BIT32(5) /* "e" bit */
#define END_W0_UNCOND_ESCALATE   PPC_BIT32(6) /* "u" bit - DD2.0 */
#define END_W0_SILENT_ESCALATE   PPC_BIT32(7) /* "s" bit - DD2.0 */
#define END_W0_QSIZE             PPC_BITMASK32(12, 15)
#define END_W0_SW0               PPC_BIT32(16)
#define END_W0_FIRMWARE          END_W0_SW0 /* Owned by FW */
#define END_QSIZE_4K             0
#define END_QSIZE_64K            4
#define END_W0_HWDEP             PPC_BITMASK32(24, 31)
        uint32_t        w1;
#define END_W1_ESn               PPC_BITMASK32(0, 1)
#define END_W1_ESn_P             PPC_BIT32(0)
#define END_W1_ESn_Q             PPC_BIT32(1)
#define END_W1_ESe               PPC_BITMASK32(2, 3)
#define END_W1_ESe_P             PPC_BIT32(2)
#define END_W1_ESe_Q             PPC_BIT32(3)
#define END_W1_GENERATION        PPC_BIT32(9)
#define END_W1_PAGE_OFF          PPC_BITMASK32(10, 31)
        uint32_t        w2;
#define END_W2_MIGRATION_REG     PPC_BITMASK32(0, 3)
#define END_W2_OP_DESC_HI        PPC_BITMASK32(4, 31)
        uint32_t        w3;
#define END_W3_OP_DESC_LO        PPC_BITMASK32(0, 31)
        uint32_t        w4;
#define END_W4_ESC_END_BLOCK     PPC_BITMASK32(4, 7)
#define END_W4_ESC_END_INDEX     PPC_BITMASK32(8, 31)
        uint32_t        w5;
#define END_W5_ESC_END_DATA      PPC_BITMASK32(1, 31)
        uint32_t        w6;
#define END_W6_FORMAT_BIT        PPC_BIT32(8)
#define END_W6_NVT_BLOCK         PPC_BITMASK32(9, 12)
#define END_W6_NVT_INDEX         PPC_BITMASK32(13, 31)
        uint32_t        w7;
#define END_W7_F0_IGNORE         PPC_BIT32(0)
#define END_W7_F0_BLK_GROUPING   PPC_BIT32(1)
#define END_W7_F0_PRIORITY       PPC_BITMASK32(8, 15)
#define END_W7_F1_WAKEZ          PPC_BIT32(0)
#define END_W7_F1_LOG_SERVER_ID  PPC_BITMASK32(1, 31)
} XiveEND;

#endif /* PPC_XIVE_REGS_H */
