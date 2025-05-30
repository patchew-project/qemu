/*
 * CXL host parameter parsing routine stubs
 *
 * Copyright (c) 2022 Huawei
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_host.h"

void cxl_fmws_link_targets(CXLState *stat, Error **errp) {};
void cxl_machine_init(Object *obj, CXLState *state) {};
void cxl_hook_up_pxb_registers(PCIBus *bus, CXLState *state, Error **errp) {};
void cxl_fixed_memory_window_config(CXLState *cxl_state,
                        CXLFixedMemoryWindowOptions *object, Error **errp) {};

const MemoryRegionOps cfmws_ops;
