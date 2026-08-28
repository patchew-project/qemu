/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI internal service helpers.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#ifndef HW_MISC_RISCV_RPMI_INTERNAL_H
#define HW_MISC_RISCV_RPMI_INTERNAL_H

#include "qapi/error.h"
#include "hw/misc/riscv_rpmi.h"
#include "librpmi.h"

#define RPMI_PLAT_INFO "QEMU RISC-V RPMI"

extern const struct rpmi_shmem_platform_ops rpmi_shmem_qemu_ops;
bool riscv_rpmi_service_enabled(RiscvRpmiState *s,
                                enum rpmi_servicegroup_id service_group);
bool riscv_rpmi_context_add_group(RiscvRpmiState *s,
                                  struct rpmi_service_group *group,
                                  const char *name,
                                  Error **errp);
void riscv_rpmi_context_remove_group(RiscvRpmiState *s,
                                     struct rpmi_service_group *group);

bool riscv_rpmi_sysreset_add(RiscvRpmiState *s, Error **errp);
void riscv_rpmi_sysreset_remove(RiscvRpmiState *s);

bool riscv_rpmi_hsm_add(RiscvRpmiState *s, Error **errp);
void riscv_rpmi_hsm_remove(RiscvRpmiState *s);
void riscv_rpmi_hsm_reset(RiscvRpmiState *s);

#endif
