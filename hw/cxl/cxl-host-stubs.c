/*
 * CXL host parameter parsing routine stubs
 *
 * Copyright (c) 2022 Huawei
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "hw/cxl/cxl.h"

QemuOptsList qemu_cxl_fixed_window_opts = {
    .name = "cxl-fixed-memory-window",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_cxl_fixed_window_opts.head),
    .desc = { { 0 } }
};

void parse_cxl_fixed_memory_window_opts(MachineState *ms) {};

void cxl_fixed_memory_window_link_targets(Error **errp) {};

const MemoryRegionOps cfmws_ops;
