/*
 * QEMU ARC CPU -- internal functions
 *
 * Copyright (c) 2019 Synopsys
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 *
 * This header shares funtions prototypes between codes in
 * target/arc/ that must remain private to it and not to be
 * used by QEMU itself.
 */

#ifndef TARGET_ARC_INTERNALS_H
#define TARGET_ARC_INTERNALS_H

#include "cpu.h"

/* add auxiliary registers to set of supported registers for GDB */
void arc_cpu_register_gdb_regs_for_features(ARCCPU *cpu);

/* these are the helper functions used both by translation and gdbstub */
target_ulong helper_lr(CPUARCState *env, uint32_t aux);
void helper_sr(CPUARCState *env, uint32_t val, uint32_t aux);

#endif
