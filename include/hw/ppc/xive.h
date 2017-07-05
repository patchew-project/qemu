/*
 * QEMU PowerPC XIVE model
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_XIVE_H
#define PPC_XIVE_H

#include "hw/ppc/xics.h"

typedef struct XIVE XIVE;
typedef struct XiveICSState XiveICSState;
typedef struct XiveICPState XiveICPState;

#define TYPE_XIVE "xive"
#define XIVE(obj) OBJECT_CHECK(XIVE, (obj), TYPE_XIVE)

#define TYPE_ICS_XIVE "xive-source"
#define ICS_XIVE(obj) OBJECT_CHECK(XiveICSState, (obj), TYPE_ICS_XIVE)

/*
 * XIVE Interrupt source flags
 */
#define XIVE_SRC_H_INT_ESB     (1ull << (63 - 60))
#define XIVE_SRC_LSI           (1ull << (63 - 61))
#define XIVE_SRC_TRIGGER       (1ull << (63 - 62))
#define XIVE_SRC_STORE_EOI     (1ull << (63 - 63))

#define TYPE_XIVE_ICP "xive-icp"
#define XIVE_ICP(obj) OBJECT_CHECK(XiveICPState, (obj), TYPE_XIVE_ICP)

struct XiveICSState {
    ICSState parent_obj;

    uint64_t     flags;
    uint32_t     esb_shift;
    hwaddr       esb_base;
    MemoryRegion esb_iomem;

    XIVE         *xive;
};

/* Number of Thread Management Interrupt Areas */
#define XIVE_TM_RING_COUNT 4

struct XiveICPState {
    ICPState parent_obj;

    uint8_t tima[XIVE_TM_RING_COUNT * 0x10];
    uint8_t *tima_os;
};

typedef struct sPAPRMachineState sPAPRMachineState;

void xive_spapr_init(sPAPRMachineState *spapr);
void xive_spapr_populate(XIVE *x, void *fdt);

void xive_mmio_map(XIVE *x);

void xive_ics_create(XiveICSState *xs, XIVE *x, uint32_t offset,
                     uint32_t nr_irqs, uint32_t shift, uint32_t flags,
                     Error **errp);

#endif /* PPC_XIVE_H */
