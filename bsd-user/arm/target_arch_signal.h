/*
 *  arm signal definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _TARGET_ARCH_SIGNAL_H_
#define _TARGET_ARCH_SIGNAL_H_

#include "cpu.h"

#define TARGET_REG_R0   0
#define TARGET_REG_R1   1
#define TARGET_REG_R2   2
#define TARGET_REG_R3   3
#define TARGET_REG_R4   4
#define TARGET_REG_R5   5
#define TARGET_REG_R6   6
#define TARGET_REG_R7   7
#define TARGET_REG_R8   8
#define TARGET_REG_R9   9
#define TARGET_REG_R10  10
#define TARGET_REG_R11  11
#define TARGET_REG_R12  12
#define TARGET_REG_R13  13
#define TARGET_REG_R14  14
#define TARGET_REG_R15  15
#define TARGET_REG_CPSR 16
#define TARGET__NGREG   17
/* Convenience synonyms */
#define TARGET_REG_FP   TARGET_REG_R11
#define TARGET_REG_SP   TARGET_REG_R13
#define TARGET_REG_LR   TARGET_REG_R14
#define TARGET_REG_PC   TARGET_REG_R15

#define TARGET_INSN_SIZE    4       /* arm instruction size */

/* Size of the signal trampolin code. See _sigtramp(). */
#define TARGET_SZSIGCODE    ((abi_ulong)(9 * TARGET_INSN_SIZE))

/* compare to arm/include/_limits.h */
#define TARGET_MINSIGSTKSZ  (1024 * 4)                  /* min sig stack size */
#define TARGET_SIGSTKSZ     (TARGET_MINSIGSTKSZ + 32768)  /* recommended size */

/* arm/arm/machdep.c */
struct target_sigcontext {
    target_sigset_t sc_mask;    /* signal mask to retstore */
    int32_t     sc_onstack;     /* sigstack state to restore */
    abi_long    sc_pc;          /* pc at time of signal */
    abi_long    sc_reg[32];     /* processor regs 0 to 31 */
    abi_long    mullo, mulhi;   /* mullo and mulhi registers */
    int32_t     sc_fpused;      /* fp has been used */
    abi_long    sc_fpregs[33];  /* fp regs 0 to 31 & csr */
    abi_long    sc_fpc_eir;     /* fp exception instr reg */
    /* int32_t reserved[8]; */
};

typedef struct {
    uint32_t    __fp_fpsr;
    struct {
        uint32_t    __fp_exponent;
        uint32_t    __fp_mantissa_hi;
        uint32_t    __fp_mantissa_lo;
    }       __fp_fr[8];
} target__fpregset_t;

typedef struct {
    uint32_t    __vfp_fpscr;
    uint32_t    __vfp_fstmx[33];
    uint32_t    __vfp_fpsid;
} target__vfpregset_t;

typedef struct target_mcontext {
    uint32_t        __gregs[TARGET__NGREG];
    union {
        target__fpregset_t  __fpregs;
        target__vfpregset_t __vfpregs;
    } __fpu;
} target_mcontext_t;

#endif /* !_TARGET_ARCH_SIGNAL_H_ */
