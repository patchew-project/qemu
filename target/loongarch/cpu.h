/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef LOONGARCH_CPU_H
#define LOONGARCH_CPU_H

#include "exec/cpu-defs.h"
#include "fpu/softfloat-types.h"

#define TCG_GUEST_DEFAULT_MO (0)

#define FCSR0_M1    0x1f         /* FCSR1 mask, Enables */
#define FCSR0_M2    0x1f1f0000   /* FCSR2 mask, Cause and Flags */
#define FCSR0_M3    0x300        /* FCSR3 mask, Round Mode */
#define FCSR0_RM    8            /* Round Mode bit num on fcsr0 */
#define GET_FP_CAUSE(reg)        (((reg) >> 24) & 0x1f)
#define GET_FP_ENABLE(reg)       (reg & 0x1f)
#define GET_FP_FLAGS(reg)        (((reg) >> 16) & 0x1f)
#define SET_FP_CAUSE(reg, v)      do { (reg) = ((reg) & ~(0x1f << 24)) | \
                                               ((v & 0x1f) << 24);       \
                                     } while (0)
#define SET_FP_ENABLE(reg, v)     do { (reg) = ((reg) & ~(0x1f) | (v & 0x1f); \
                                     } while (0)
#define SET_FP_FLAGS(reg, v)      do { (reg) = ((reg) & ~(0x1f << 16)) | \
                                               ((v & 0x1f) << 16);       \
                                     } while (0)
#define UPDATE_FP_FLAGS(reg, v)   do { (reg) |= ((v & 0x1f) << 16); } while (0)

#define FP_INEXACT        1
#define FP_UNDERFLOW      2
#define FP_OVERFLOW       4
#define FP_DIV0           8
#define FP_INVALID        16

extern const char * const regnames[];
extern const char * const fregnames[];

typedef struct CPULoongArchState CPULoongArchState;
struct CPULoongArchState {
    uint64_t gpr[32];
    uint64_t pc;

    uint64_t fpr[32];
    float_status fp_status;
    bool cf[8];

    /*
     * fcsr0
     * 31:29 |28:24 |23:21 |20:16 |15:10 |9:8 |7:5 |4:0
     *        Cause         Flags         RM        Enables
     */
    uint32_t fcsr0;
    uint32_t fcsr0_mask;

    uint32_t cpucfg[15];

    uint64_t lladdr; /* LL virtual address compared against SC */
    uint64_t llval;

    uint64_t badaddr;
};

/**
 * LoongArchCPU:
 * @env: #CPULoongArchState
 * @clock: this CPU input clock (may be connected
 *         to an output clock from another device).
 *
 * A LoongArch CPU.
 */
struct LoongArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPULoongArchState env;
};

#define TYPE_LOONGARCH_CPU "loongarch64-cpu"

OBJECT_DECLARE_TYPE(LoongArchCPU, LoongArchCPUClass,
                    LOONGARCH_CPU)

/**
 * LoongArchCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A LoongArch CPU model.
 */
struct LoongArchCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

static inline void cpu_get_tb_cpu_state(CPULoongArchState *env,
                                        target_ulong *pc,
                                        target_ulong *cs_base,
                                        uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
}

void loongarch_cpu_list(void);

#define cpu_signal_handler cpu_loongarch_signal_handler
#define cpu_list loongarch_cpu_list

#define MMU_USER_IDX 3

typedef CPULoongArchState CPUArchState;
typedef LoongArchCPU ArchCPU;

#include "exec/cpu-all.h"

/* Exceptions */
enum {
    EXCP_NONE          = -1,
    EXCP_ADE           = 0,
    EXCP_SYSCALL,
    EXCP_BREAK,
    EXCP_INE,
    EXCP_FPE,

    EXCP_LAST = EXCP_FPE,
};

int cpu_loongarch_signal_handler(int host_signum, void *pinfo, void *puc);

#define LOONGARCH_CPU_TYPE_SUFFIX "-" TYPE_LOONGARCH_CPU
#define LOONGARCH_CPU_TYPE_NAME(model) model LOONGARCH_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_LOONGARCH_CPU

#endif /* LOONGARCH_CPU_H */
