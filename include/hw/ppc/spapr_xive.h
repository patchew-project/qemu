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

    /* Internal interrupt source for IPIs and virtual devices */
    XiveSource   source;

    /* Routing table */
    XiveIVE      *ivt;
    uint32_t     nr_irqs;

    /* TIMA memory regions */
    hwaddr       tm_base;
    MemoryRegion tm_mmio_user;
    MemoryRegion tm_mmio_os;
} sPAPRXive;

#define SPAPR_XIVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(sPAPRXiveClass, (klass), TYPE_SPAPR_XIVE)
#define SPAPR_XIVE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(sPAPRXiveClass, (obj), TYPE_SPAPR_XIVE)

typedef struct sPAPRXiveClass {
    SysBusDeviceClass parent_class;

    void (*synchronize_state)(sPAPRXive *xive);
    void (*pre_save)(sPAPRXive *xsrc);
    int (*post_load)(sPAPRXive *xsrc, int version_id);
} sPAPRXiveClass;

bool spapr_xive_irq_enable(sPAPRXive *xive, uint32_t lisn, bool lsi);
bool spapr_xive_irq_disable(sPAPRXive *xive, uint32_t lisn);
void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon);
void spapr_xive_mmio_map(sPAPRXive *xive);
void spapr_xive_mmio_unmap(sPAPRXive *xive);
qemu_irq spapr_xive_qirq(sPAPRXive *xive, int lisn);
void spapr_xive_common_realize(sPAPRXive *xive, int esb_shift, Error **errp);

/*
 * sPAPR encoding of EQ indexes
 */
#define SPAPR_XIVE_EQ_INDEX(server, prio)  (((server) << 3) | ((prio) & 0x7))
#define SPAPR_XIVE_EQ_SERVER(eq_idx) ((eq_idx) >> 3)
#define SPAPR_XIVE_EQ_PRIO(eq_idx)   ((eq_idx) & 0x7)

typedef struct sPAPRMachineState sPAPRMachineState;

void spapr_xive_hcall_init(sPAPRMachineState *spapr);
void spapr_dt_xive(sPAPRMachineState *spapr, int nr_servers, void *fdt,
                   uint32_t phandle);

#endif /* PPC_SPAPR_XIVE_H */
