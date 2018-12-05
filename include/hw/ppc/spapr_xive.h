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

#include "hw/ppc/xive.h"

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(sPAPRXive, (obj), TYPE_SPAPR_XIVE)

typedef struct sPAPRXive {
    XiveRouter    parent;

    /* Internal interrupt source for IPIs and virtual devices */
    XiveSource    source;
    hwaddr        vc_base;

    /* END ESB MMIOs */
    XiveENDSource end_source;
    hwaddr        end_base;

    /* Routing table */
    XiveEAS       *eat;
    uint32_t      nr_irqs;
    XiveEND       *endt;
    uint32_t      nr_ends;

    /* TIMA mapping address */
    hwaddr        tm_base;
    MemoryRegion  tm_mmio;

    /* KVM support */
    int           fd;
    void          *tm_mmap;
} sPAPRXive;

bool spapr_xive_irq_claim(sPAPRXive *xive, uint32_t lisn, bool lsi);
bool spapr_xive_irq_free(sPAPRXive *xive, uint32_t lisn);
void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon);
qemu_irq spapr_xive_qirq(sPAPRXive *xive, uint32_t lisn);
bool spapr_xive_priority_is_reserved(uint8_t priority);

void spapr_xive_cpu_to_nvt(sPAPRXive *xive, PowerPCCPU *cpu,
                           uint8_t *out_nvt_blk, uint32_t *out_nvt_idx);
void spapr_xive_cpu_to_end(sPAPRXive *xive, PowerPCCPU *cpu, uint8_t prio,
                           uint8_t *out_end_blk, uint32_t *out_end_idx);
int spapr_xive_target_to_end(sPAPRXive *xive, uint32_t target, uint8_t prio,
                             uint8_t *out_end_blk, uint32_t *out_end_idx);

typedef struct sPAPRMachineState sPAPRMachineState;

void spapr_xive_hcall_init(sPAPRMachineState *spapr);
void spapr_dt_xive(sPAPRMachineState *spapr, uint32_t nr_servers, void *fdt,
                   uint32_t phandle);
void spapr_xive_reset_tctx(sPAPRXive *xive);
void spapr_xive_map_mmio(sPAPRXive *xive);

/*
 * KVM XIVE device helpers
 */
void kvmppc_xive_connect(sPAPRXive *xive, Error **errp);
void kvmppc_xive_synchronize_state(sPAPRXive *xive);

#endif /* PPC_SPAPR_XIVE_H */
