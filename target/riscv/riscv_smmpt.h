/*
 * QEMU RISC-V Smmpt (Memory Protection Table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_SMMPT_H
#define RISCV_SMMPT_H

typedef enum {
    SMMPTBARE = 0,
    SMMPT34   = 1,
    SMMPT43   = 2,
    SMMPT52   = 3,
    SMMPT64   = 4,
    SMMPTMAX
} mpt_mode_t;

#endif
