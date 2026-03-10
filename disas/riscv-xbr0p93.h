/*
 * QEMU RISC-V Disassembler for xbr0p93 matching the unratified Zbr CRC32
 * bitmanip extension v0.93.
 *
 * Copyright (c) 2023 Rivos Inc
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_XBR0P93_H
#define DISAS_RISCV_XBR0P93_H

#include "disas/riscv.h"

extern const rv_opcode_data rv_xbr0p93_opcode_data[];

void decode_xbr0p93(rv_decode *, rv_isa);

#endif /* DISAS_RISCV_XBR0P93_H */
