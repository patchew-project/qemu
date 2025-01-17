/*
 * QEMU CXL Host Setup
 *
 * Copyright (c) 2022 Huawei
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "hw/cxl/cxl.h"
#include "hw/boards.h"

#ifndef CXL_HOST_H
#define CXL_HOST_H

void cxl_machine_init(Object *obj, CXLState *state);
void cxl_fmws_link_targets(CXLState *stat, Error **errp);
void cxl_hook_up_pxb_registers(PCIBus *bus, CXLState *state, Error **errp);
void cxl_fixed_memory_window_config(CXLState *cxl_state,
                        CXLFixedMemoryWindowOptions *object, Error **errp);

int cxl_host_set_irq_num(CXLHostBridge *host, int index, int gsi);
void cxl_host_hook_up_registers(CXLState *cxl_state, CXLHostBridge *host,
                                Error **errp);

extern const MemoryRegionOps cfmws_ops;

#endif
