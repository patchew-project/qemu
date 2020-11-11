/*
 * QEMU ARC CPU -- internal functions
 *
 * Copyright (c) 2019 Synopsys Inc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
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
