/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI device-tree helpers
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#ifndef HW_RISCV_RPMI_FDT_H
#define HW_RISCV_RPMI_FDT_H

#include "exec/hwaddr.h"
#include "hw/misc/riscv_rpmi.h"

typedef struct RiscvRpmiFdtMboxConfig {
    hwaddr shmem_base;
    hwaddr doorbell_base;
    uint32_t a2p_req_size;
    uint32_t p2a_req_size;
    uint32_t doorbell_size;
} RiscvRpmiFdtMboxConfig;

void riscv_rpmi_fdt_add_mbox(void *fdt,
                             const RiscvRpmiFdtMboxConfig *cfg,
                             uint32_t *phandle,
                             uint32_t *mbox_handle);

#endif
