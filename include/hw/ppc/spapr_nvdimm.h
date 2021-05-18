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
#include "migration/vmstate.h"

#define TYPE_SPAPR_NVDIMM "spapr-nvdimm"
OBJECT_DECLARE_SIMPLE_TYPE(SpaprNVDIMMDevice, SPAPR_NVDIMM)

typedef struct SpaprNVDIMMDevice  SpaprNVDIMMDevice;
typedef struct SpaprDrc SpaprDrc;
typedef struct SpaprMachineState SpaprMachineState;

int spapr_pmem_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp);
void spapr_dt_persistent_memory(SpaprMachineState *spapr, void *fdt);
bool spapr_nvdimm_validate(HotplugHandler *hotplug_dev, NVDIMMDevice *nvdimm,
                           uint64_t size, Error **errp);
void spapr_add_nvdimm(DeviceState *dev, uint64_t slot);
void spapr_nvdimm_finish_flushes(SpaprMachineState *spapr);

typedef struct SpaprNVDIMMDeviceFlushState {
    uint64_t continue_token;
    int64_t hcall_ret;
    int backend_fd;
    uint32_t drcidx;

    QLIST_ENTRY(SpaprNVDIMMDeviceFlushState) node;
} SpaprNVDIMMDeviceFlushState;

extern const VMStateDescription vmstate_spapr_nvdimm_states;

#endif
