/*
 * QEMU PowerPC pSeries Logical Partition NUMA associativity handling
 *
 * Copyright IBM Corp. 2020
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_NUMA_H
#define HW_SPAPR_NUMA_H

#include "hw/boards.h"
#include "hw/ppc/spapr.h"

void spapr_numa_associativity_reset(SpaprMachineState *spapr);
void spapr_numa_write_rtas_dt(SpaprMachineState *spapr, void *fdt, int rtas);
void spapr_numa_write_associativity_dt(SpaprMachineState *spapr, void *fdt,
                                       int offset, int nodeid);
int spapr_numa_fixup_cpu_dt(SpaprMachineState *spapr, void *fdt,
                            int offset, PowerPCCPU *cpu);
int spapr_numa_write_assoc_lookup_arrays(SpaprMachineState *spapr, void *fdt,
                                         int offset);
unsigned int spapr_numa_initial_nvgpu_numa_id(MachineState *machine);
void spapr_numa_check_FORM1_constraints(MachineState *machine);

#endif /* HW_SPAPR_NUMA_H */
