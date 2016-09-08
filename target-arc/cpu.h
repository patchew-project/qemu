 /*
 * QEMU ARC CPU
 *
 * Copyright (c) 2016 Michael Rolnik
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#if !defined(CPU_ARC_H)
#define CPU_ARC_H

#include "qemu-common.h"

#define TARGET_LONG_BITS            32

#define CPUArchState struct CPUARCState

#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

#define TARGET_PAGE_BITS            12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#define NB_MMU_MODES                1

#define MMU_IDX                     0

#define PHYS_BASE_RAM               0x00000000
#define VIRT_BASE_RAM               0x00000000

enum    arc_features {
    ARC_FEATURE_ARC5,
    ARC_FEATURE_ARC600,
    ARC_FEATURE_ARC700,
    no_features,
};


typedef struct CPUARCState CPUARCState;
#define CPU_GP(env)     ((env)->r[26])
#define CPU_FP(env)     ((env)->r[27])
#define CPU_SP(env)     ((env)->r[28])
#define CPU_ILINK1(env) ((env)->r[29])
#define CPU_ILINK2(env) ((env)->r[30])
#define CPU_BLINK(env)  ((env)->r[31])
#define CPU_MLO(env)    ((env)->r[57])
#define CPU_MMI(env)    ((env)->r[58])
#define CPU_MHI(env)    ((env)->r[59])
#define CPU_LP(env)     ((env)->r[60])
#define CPU_IMM(env)    ((env)->r[62])
#define CPU_PCL(env)    ((env)->r[63])


struct CPUARCState {
    uint32_t        r[64];

    struct {
        uint32_t    Lf;
        uint32_t    Zf;     /*  zero                    */
        uint32_t    Nf;     /*  negative                */
        uint32_t    Cf;     /*  carry                   */
        uint32_t    Vf;     /*  overflow                */
        uint32_t    Uf;

        uint32_t    DEf;
        uint32_t    AEf;
        uint32_t    A2f;    /*  interrupt 1 is active   */
        uint32_t    A1f;    /*  interrupt 2 is active   */
        uint32_t    E2f;    /*  interrupt 1 mask        */
        uint32_t    E1f;    /*  interrupt 2 mask        */
        uint32_t    Hf;     /*  halt                    */
    } stat, stat_l1, stat_l2, stat_er;

    struct {
        uint32_t    S2;
        uint32_t    S1;
        uint32_t    CS;
    } macmod;

    uint32_t        intvec;

    uint32_t        eret;
    uint32_t        erbta;
    uint32_t        ecr;
    uint32_t        efa;
    uint32_t        bta;
    uint32_t        bta_l1;
    uint32_t        bta_l2;

    uint32_t        pc;     /*  program counter         */
    uint32_t        lps;    /*  loops start             */
    uint32_t        lpe;    /*  loops end               */

    struct {
        uint32_t    LD;     /*  load pending bit        */
        uint32_t    SH;     /*  self halt               */
        uint32_t    BH;     /*  breakpoint halt         */
        uint32_t    UB;     /*  user mode break enabled */
        uint32_t    ZZ;     /*  sleep mode              */
        uint32_t    RA;     /*  reset applied           */
        uint32_t    IS;     /*  single instruction step */
        uint32_t    FH;     /*  force halt              */
        uint32_t    SS;     /*  single step             */
    } debug;
    uint32_t        features;
    bool            stopped;

    /* Those resources are used only in QEMU core */
    CPU_COMMON
};

static inline int arc_feature(CPUARCState *env, int feature)
{
    return (env->features & (1U << feature)) != 0;
}

static inline void  arc_set_feature(CPUARCState *env, int feature)
{
    env->features |= (1U << feature);
}

#define cpu_list            arc_cpu_list
#define cpu_signal_handler  cpu_arc_signal_handler

#include "exec/cpu-all.h"
#include "cpu-qom.h"

static inline int cpu_mmu_index(CPUARCState *env, bool ifetch)
{
    return  0;
}

void arc_translate_init(void);

ARCCPU *cpu_arc_init(const char *cpu_model);

#define cpu_init(cpu_model) CPU(cpu_arc_init(cpu_model))

void arc_cpu_list(FILE *f, fprintf_function cpu_fprintf);
int cpu_arc_exec(CPUState *cpu);
int cpu_arc_signal_handler(int host_signum, void *pinfo, void *puc);
int arc_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int rw,
                                int mmu_idx);
int arc_cpu_memory_rw_debug(CPUState *cs, vaddr address, uint8_t *buf,
                                int len, bool is_write);

static inline void cpu_get_tb_cpu_state(CPUARCState *env, target_ulong *pc,
                                target_ulong *cs_base, uint32_t *pflags)
{
    *pc = env->pc;
    *cs_base = 0;
    *pflags = 0;
}

static inline int cpu_interrupts_enabled(CPUARCState *env1)
{
    return  0;
}

#include "exec/exec-all.h"

#endif /* !defined (CPU_ARC_H) */
