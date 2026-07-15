/*
 * QEMU RISC-V Smmpt (Memory Protection Table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_SMMPT_H
#define RISCV_SMMPT_H

#include "cpu.h"
#include "exec/mmu-access-type.h"

typedef enum {
    SMMPTBARE = 0,
    SMMPT34   = 1,
    SMMPT43   = 2,
    SMMPT52   = 3,
    SMMPT64   = 4,
    SMMPTMAX
} mpt_mode_t;

bool smmpt_check_access(CPURISCVState *env, hwaddr addr,
                        int *prot, MMUAccessType access_type);
#endif
