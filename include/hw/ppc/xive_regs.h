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

#endif /* PPC_XIVE_REGS_H */
