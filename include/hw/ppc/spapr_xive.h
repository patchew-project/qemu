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

#define TYPE_SPAPR_XIVE_BASE "spapr-xive-base"
#define SPAPR_XIVE_BASE(obj) \
    OBJECT_CHECK(sPAPRXive, (obj), TYPE_SPAPR_XIVE_BASE)

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
} sPAPRXive;

#define SPAPR_XIVE_BASE_CLASS(klass) \
     OBJECT_CLASS_CHECK(sPAPRXiveClass, (klass), TYPE_SPAPR_XIVE_BASE)
#define SPAPR_XIVE_BASE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(sPAPRXiveClass, (obj), TYPE_SPAPR_XIVE_BASE)

typedef struct sPAPRXiveClass {
    XiveRouterClass parent_class;

    DeviceRealize   parent_realize;
} sPAPRXiveClass;

bool spapr_xive_irq_enable(sPAPRXive *xive, uint32_t lisn, bool lsi);
bool spapr_xive_irq_disable(sPAPRXive *xive, uint32_t lisn);
void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon);
qemu_irq spapr_xive_qirq(sPAPRXive *xive, uint32_t lisn);

/*
 * sPAPR NVT and END indexing helpers
 */
uint32_t spapr_xive_nvt_to_target(sPAPRXive *xive, uint8_t nvt_blk,
                                  uint32_t nvt_idx);
int spapr_xive_target_to_nvt(sPAPRXive *xive, uint32_t target,
                            uint8_t *out_nvt_blk, uint32_t *out_nvt_idx);
int spapr_xive_cpu_to_nvt(sPAPRXive *xive, PowerPCCPU *cpu,
                          uint8_t *out_nvt_blk, uint32_t *out_nvt_idx);

int spapr_xive_end_to_target(sPAPRXive *xive, uint8_t end_blk, uint32_t end_idx,
                             uint32_t *out_server, uint8_t *out_prio);
int spapr_xive_target_to_end(sPAPRXive *xive, uint32_t target, uint8_t prio,
                             uint8_t *out_end_blk, uint32_t *out_end_idx);
int spapr_xive_cpu_to_end(sPAPRXive *xive, PowerPCCPU *cpu, uint8_t prio,
                          uint8_t *out_end_blk, uint32_t *out_end_idx);

bool spapr_xive_priority_is_valid(uint8_t priority);

typedef struct sPAPRMachineState sPAPRMachineState;

void spapr_xive_hcall_init(sPAPRMachineState *spapr);
void spapr_dt_xive(sPAPRXive *xive, int nr_servers, void *fdt,
                   uint32_t phandle);

#endif /* PPC_SPAPR_XIVE_H */
