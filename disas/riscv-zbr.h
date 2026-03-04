/*
 * QEMU RISC-V Disassembler for Zbr v0.93 (unratified)
 *
 * Copyright (c) 2023 Rivos Inc
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_ZBR_H
#define DISAS_RISCV_ZBR_H

#include "disas/riscv.h"

extern const rv_opcode_data rv_zbr_opcode_data[];

void decode_zbr(rv_decode *, rv_isa);

#endif /* DISAS_RISCV_ZBR_H */
