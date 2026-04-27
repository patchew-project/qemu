/*
 * QEMU CXL Host Setup
 *
 * Copyright (c) 2022 Huawei
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "hw/cxl/cxl.h"
#include "hw/core/boards.h"

#ifndef CXL_HOST_H
#define CXL_HOST_H

void cxl_machine_init(Object *obj, CXLState *state);
void cxl_fmws_link_targets(Error **errp);
void cxl_hook_up_pxb_registers(PCIBus *bus, CXLState *state, Error **errp);
hwaddr cxl_fmws_set_memmap(hwaddr base, hwaddr max_addr);
void cxl_fmws_update_mmio(void);
GSList *cxl_fmws_get_all_sorted(void);

/**
 * cxl_fmws_base - GPA base of the first CXL Fixed Memory Window region.
 *
 * Set by cxl_fmws_set_memmap() to the base address it receives (typically
 * ROUND_UP(highest_gpa + 1, 256 MiB) on ARM virt). Valid after the
 * machine memory-map init callback returns, i.e. at machine_done time.
 * Zero when no machine has called cxl_fmws_set_memmap() (stub builds).
 */
extern hwaddr cxl_fmws_base;

extern const MemoryRegionOps cfmws_ops;

#endif
