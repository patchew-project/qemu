/*
 * QEMU RISC-V Smmpt (Memory Protection Table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_SMPT_H
#define RISCV_SMPT_H

#include "cpu.h"
#include "exec/mmu-access-type.h"

typedef enum {
    SMMTTBARE = 0,
    SMMTT34   = 1,
    SMMTT43   = 2,
    SMMTT52   = 3,
    SMMTT64   = 4,
    SMMTTMAX
} mpt_mode_t;

typedef enum {
    ACCESS_ALLOW_R = 0b001,
    ACCESS_ALLOW_X = 0b100,
    ACCESS_ALLOW_RX = 0b101 ,
    ACCESS_ALLOW_RW = 0b011,
    ACCESS_ALLOW_RWX = 0b111,
} mpt_access_t;


int smmpt_access_to_page_prot(mpt_access_t mpt_access);
bool smmpt_check_access(CPURISCVState *env, hwaddr addr,
                        mpt_access_t *allowed_access,
                        MMUAccessType access_type);

#endif
