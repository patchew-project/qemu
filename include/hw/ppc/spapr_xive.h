/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_SPAPR_XIVE_H
#define PPC_SPAPR_XIVE_H

#include "hw/sysbus.h"
#include "hw/ppc/xive.h"

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(sPAPRXive, (obj), TYPE_SPAPR_XIVE)

typedef struct sPAPRXive {
    SysBusDevice parent;

    /* Routing table */
    XiveIVE      *ivt;
    uint32_t     nr_irqs;
} sPAPRXive;

bool spapr_xive_irq_enable(sPAPRXive *xive, uint32_t lisn, bool lsi);
bool spapr_xive_irq_disable(sPAPRXive *xive, uint32_t lisn);
void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon);

#endif /* PPC_SPAPR_XIVE_H */
