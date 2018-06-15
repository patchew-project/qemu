/*
 * QEMU PowerPC sPAPR IRQ backend definitions
 *
 * Copyright (c) 2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#ifndef HW_SPAPR_IRQ_H
#define HW_SPAPR_IRQ_H

/*
 * IRQ ranges per type
 */
#define SPAPR_IRQ_EPOW       0x1000  /* XICS_IRQ_BASE offset */
#define SPAPR_IRQ_HOTPLUG    0x1001
#define SPAPR_IRQ_VIO        0x1040
#define SPAPR_IRQ_PCI_LSI    0x1080

#define SPAPR_IRQ_MSI        0x1100  /* Offset of the dynamic range covered
                                      * by the bitmap allocator */

void spapr_irq_msi_init(sPAPRMachineState *spapr, uint32_t nr_irqs,
                       Error **errp);
int spapr_irq_msi_alloc(sPAPRMachineState *spapr, uint32_t num, bool align,
                        Error **errp);
void spapr_irq_msi_free(sPAPRMachineState *spapr, int irq, uint32_t num);
void spapr_irq_msi_reset(sPAPRMachineState *spapr);

#endif
