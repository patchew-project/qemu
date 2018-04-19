/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2016-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef _PPC_XIVE_REGS_H
#define _PPC_XIVE_REGS_H

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

#endif /* _INTC_XIVE_INTERNAL_H */
