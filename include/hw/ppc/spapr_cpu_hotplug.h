/*
 * sPAPR CPU hotplug helpers.
 *
 * Declarations for functions that manage CPU device-tree generation,
 * VSMT/VCPU-id mapping, CPU-slot lookup, machine initialisation, and
 * the HotplugHandler / PPCVirtualHypervisor callbacks for sPAPR CPU cores.
 *
 * Copyright (c) 2010-2024 IBM Corporation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SPAPR_CPU_HOTPLUG_H
#define HW_SPAPR_CPU_HOTPLUG_H

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_drc.h"
#include "target/ppc/cpu-qom.h"
#include "hw/core/hotplug.h"

/* ---- VCPU-id / VSMT helpers ------------------------------------------ */

/*
 * These two functions implement the VCPU-id numbering: one to compute
 * them all, one to identify thread 0 of a vcore.  Any change to the
 * first is likely to affect the second, so they live together.
 */
int  spapr_vcpu_id(SpaprMachineState *spapr, int cpu_index);
bool spapr_is_thread0_in_vcore(SpaprMachineState *spapr, PowerPCCPU *cpu);

/* ---- CPU-slot lookup ------------------------------------------------- */

/* Find the CPUArchId slot in machine->possible_cpus by core_id. */
CPUArchId *spapr_find_cpu_slot(MachineState *ms, uint32_t id, int *idx);

/* ---- Machine-initialisation helpers ---------------------------------- */

void spapr_set_vsmt_mode(SpaprMachineState *spapr, Error **errp);
void spapr_init_cpus(SpaprMachineState *spapr);

/* ---- Device-tree helpers --------------------------------------------- */

/*
 * spapr_dt_cpu() fills the FDT node at @offset for vCPU @cs, including
 * ibm,my-drc-index, pa-features, interrupt-server#s, etc.  It is called
 * both during boot-time FDT construction and from spapr_core_dt_populate()
 * during CPU hotplug.
 */
void spapr_dt_cpu(CPUState *cs, void *fdt, int offset,
                  SpaprMachineState *spapr);
void spapr_dt_cpus(void *fdt, SpaprMachineState *spapr);

/* ---- HotplugHandler callbacks ---------------------------------------- */

void spapr_core_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                         Error **errp);
void spapr_core_plug(HotplugHandler *hotplug_dev, DeviceState *dev);
void spapr_core_unplug(HotplugHandler *hotplug_dev, DeviceState *dev);
void spapr_core_unplug_request(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp);

/* ---- Machine-class CPU-topology callbacks ----------------------------- */

CpuInstanceProperties spapr_cpu_index_to_props(MachineState *machine,
                                               unsigned cpu_index);
int64_t spapr_get_default_cpu_node_id(const MachineState *ms, int idx);
const CPUArchIdList *spapr_possible_cpu_arch_ids(MachineState *machine);

/* ---- PPCVirtualHypervisor callbacks ---------------------------------- */

bool spapr_cpu_in_nested(PowerPCCPU *cpu);
void spapr_cpu_exec_enter(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu);
void spapr_cpu_exec_exit(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu);

#endif /* HW_SPAPR_CPU_HOTPLUG_H */
