/*
 * x86 gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef I386_GDB_INTERNAL_H
#define I386_GDB_INTERNAL_H

/*
 * Keep these in sync with assignment to
 * gdb_num_core_regs in target/i386/cpu.c
 * and with the machine description
 */

/*
 * SEG: 6 segments, plus fs_base, gs_base, kernel_gs_base
 */

/*
 * general regs ----->  8 or 16
 */
#define IDX_NB_IP       1
#define IDX_NB_FLAGS    1
#define IDX_NB_SEG      (6 + 3)
#define IDX_NB_CTL      6
#define IDX_NB_FP       16
/*
 * fpu regs ----------> 8 or 16
 */
#define IDX_NB_MXCSR    1
/*
 *          total ----> 8+1+1+9+6+16+8+1=50 or 16+1+1+9+6+16+16+1=66
 */

#define IDX_IP_REG      CPU_NB_REGS
#define IDX_FLAGS_REG   (IDX_IP_REG + IDX_NB_IP)
#define IDX_SEG_REGS    (IDX_FLAGS_REG + IDX_NB_FLAGS)
#define IDX_CTL_REGS    (IDX_SEG_REGS + IDX_NB_SEG)
#define IDX_FP_REGS     (IDX_CTL_REGS + IDX_NB_CTL)
#define IDX_XMM_REGS    (IDX_FP_REGS + IDX_NB_FP)
#define IDX_MXCSR_REG   (IDX_XMM_REGS + CPU_NB_REGS)

#define IDX_CTL_CR0_REG     (IDX_CTL_REGS + 0)
#define IDX_CTL_CR2_REG     (IDX_CTL_REGS + 1)
#define IDX_CTL_CR3_REG     (IDX_CTL_REGS + 2)
#define IDX_CTL_CR4_REG     (IDX_CTL_REGS + 3)
#define IDX_CTL_CR8_REG     (IDX_CTL_REGS + 4)
#define IDX_CTL_EFER_REG    (IDX_CTL_REGS + 5)

#endif
