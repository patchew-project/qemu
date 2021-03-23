/*
 * QEMU PowerPC PAPR SCM backend definitions
 *
 * Copyright (c) 2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_NVDIMM_H
#define HW_SPAPR_NVDIMM_H

#include "hw/mem/nvdimm.h"

struct SpaprDrc;
struct SpaprMachineState;

int spapr_pmem_dt_populate(struct SpaprDrc *drc,
                           struct SpaprMachineState *spapr, void *fdt,
                           int *fdt_start_offset, Error **errp);
void spapr_dt_persistent_memory(struct SpaprMachineState *spapr, void *fdt);
bool spapr_nvdimm_validate(HotplugHandler *hotplug_dev, NVDIMMDevice *nvdimm,
                           uint64_t size, Error **errp);
void spapr_add_nvdimm(DeviceState *dev, uint64_t slot);

#endif
