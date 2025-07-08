/*
 * s390x internal definitions and helpers
 *
 * Copyright (c) 2009 Ulrich Hecht
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390X_INTERNAL_H
#define S390X_INTERNAL_H

#include "cpu.h"
#include "fpu/softfloat.h"

#ifndef CONFIG_USER_ONLY
#include "s390x-system.h"
#endif

/* While the PoO talks about ILC (a number between 1-3) what is actually
   stored in LowCore is shifted left one bit (an even between 2-6).  As
   this is the actual length of the insn and therefore more useful, that
   is what we want to pass around and manipulate.  To make sure that we
   have applied this distinction universally, rename the "ILC" to "ILEN".  */
static inline int get_ilen(uint8_t opc)
{
    switch (opc >> 6) {
    case 0:
        return 2;
    case 1:
    case 2:
        return 4;
    default:
        return 6;
    }
}

static inline uint64_t wrap_address(CPUS390XState *env, uint64_t a)
{
    if (!(env->psw.mask & PSW_MASK_64)) {
        if (!(env->psw.mask & PSW_MASK_32)) {
            /* 24-Bit mode */
            a &= 0x00ffffff;
        } else {
            /* 31-Bit mode */
            a &= 0x7fffffff;
        }
    }
    return a;
}

/* CC optimization */

/* Instead of computing the condition codes after each x86 instruction,
 * QEMU just stores the result (called CC_DST), the type of operation
 * (called CC_OP) and whatever operands are needed (CC_SRC and possibly
 * CC_VR). When the condition codes are needed, the condition codes can
 * be calculated using this information. Condition codes are not generated
 * if they are only needed for conditional branches.
 */
enum cc_op {
    CC_OP_CONST0 = 0,           /* CC is 0 */
    CC_OP_CONST1,               /* CC is 1 */
    CC_OP_CONST2,               /* CC is 2 */
    CC_OP_CONST3,               /* CC is 3 */

    CC_OP_DYNAMIC,              /* CC calculation defined by env->cc_op */
    CC_OP_STATIC,               /* CC value is env->cc_op */

    CC_OP_NZ,                   /* env->cc_dst != 0 */
    CC_OP_ADDU,                 /* dst != 0, src = carry out (0,1) */
    CC_OP_SUBU,                 /* dst != 0, src = borrow out (0,-1) */

    CC_OP_LTGT_32,              /* signed less/greater than (32bit) */
    CC_OP_LTGT_64,              /* signed less/greater than (64bit) */
    CC_OP_LTUGTU_32,            /* unsigned less/greater than (32bit) */
    CC_OP_LTUGTU_64,            /* unsigned less/greater than (64bit) */
    CC_OP_LTGT0_32,             /* signed less/greater than 0 (32bit) */
    CC_OP_LTGT0_64,             /* signed less/greater than 0 (64bit) */

    CC_OP_ADD_64,               /* overflow on add (64bit) */
    CC_OP_SUB_64,               /* overflow on subtraction (64bit) */
    CC_OP_ABS_64,               /* sign eval on abs (64bit) */
    CC_OP_NABS_64,              /* sign eval on nabs (64bit) */
    CC_OP_MULS_64,              /* overflow on signed multiply (64bit) */

    CC_OP_ADD_32,               /* overflow on add (32bit) */
    CC_OP_SUB_32,               /* overflow on subtraction (32bit) */
    CC_OP_ABS_32,               /* sign eval on abs (64bit) */
    CC_OP_NABS_32,              /* sign eval on nabs (64bit) */
    CC_OP_MULS_32,              /* overflow on signed multiply (32bit) */

    CC_OP_COMP_32,              /* complement */
    CC_OP_COMP_64,              /* complement */

    CC_OP_TM_32,                /* test under mask (32bit) */
    CC_OP_TM_64,                /* test under mask (64bit) */

    CC_OP_NZ_F32,               /* FP dst != 0 (32bit) */
    CC_OP_NZ_F64,               /* FP dst != 0 (64bit) */
    CC_OP_NZ_F128,              /* FP dst != 0 (128bit) */

    CC_OP_ICM,                  /* insert characters under mask */
    CC_OP_SLA,                  /* Calculate shift left signed */
    CC_OP_FLOGR,                /* find leftmost one */
    CC_OP_LCBB,                 /* load count to block boundary */
    CC_OP_VC,                   /* vector compare result */
    CC_OP_MAX
};

/* cc_helper.c */
const char *cc_name(enum cc_op cc_op);
uint32_t calc_cc(CPUS390XState *env, uint32_t cc_op, uint64_t src, uint64_t dst,
                 uint64_t vr);

/* cpu_models.c */
void s390_cpu_model_class_register_props(ObjectClass *oc);
void s390_realize_cpu_model(CPUState *cs, Error **errp);
S390CPUModel *get_max_cpu_model(Error **errp);
void apply_cpu_model(const S390CPUModel *model, Error **errp);
ObjectClass *s390_cpu_class_by_name(const char *name);

void s390_cpu_do_interrupt(CPUState *cpu);

#ifdef CONFIG_USER_ONLY
void s390_cpu_record_sigsegv(CPUState *cs, vaddr address,
                             MMUAccessType access_type,
                             bool maperr, uintptr_t retaddr);
void s390_cpu_record_sigbus(CPUState *cs, vaddr address,
                            MMUAccessType access_type, uintptr_t retaddr);
#else
bool s390_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
G_NORETURN void s390x_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                              MMUAccessType access_type, int mmu_idx,
                                              uintptr_t retaddr);
#endif


/* fpu_helper.c */
uint32_t set_cc_nz_f32(float32 v);
uint32_t set_cc_nz_f64(float64 v);
uint32_t set_cc_nz_f128(float128 v);
#define S390_IEEE_MASK_INVALID   0x80
#define S390_IEEE_MASK_DIVBYZERO 0x40
#define S390_IEEE_MASK_OVERFLOW  0x20
#define S390_IEEE_MASK_UNDERFLOW 0x10
#define S390_IEEE_MASK_INEXACT   0x08
#define S390_IEEE_MASK_QUANTUM   0x04
uint8_t s390_softfloat_exc_to_ieee(unsigned int exc);
int s390_swap_bfp_rounding_mode(CPUS390XState *env, int m3);
void s390_restore_bfp_rounding_mode(CPUS390XState *env, int old_mode);
int float_comp_to_cc(CPUS390XState *env, FloatRelation float_compare);

#define DCMASK_ZERO             0x0c00
#define DCMASK_NORMAL           0x0300
#define DCMASK_SUBNORMAL        0x00c0
#define DCMASK_INFINITY         0x0030
#define DCMASK_QUIET_NAN        0x000c
#define DCMASK_SIGNALING_NAN    0x0003
#define DCMASK_NAN              0x000f
#define DCMASK_NEGATIVE         0x0555
uint16_t float32_dcmask(CPUS390XState *env, float32 f1);
uint16_t float64_dcmask(CPUS390XState *env, float64 f1);
uint16_t float128_dcmask(CPUS390XState *env, float128 f1);


/* gdbstub.c */
int s390_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int s390_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void s390_cpu_gdb_init(CPUState *cs);

void s390_cpu_dump_state(CPUState *cpu, FILE *f, int flags);

/* interrupt.c */
void trigger_pgm_exception(CPUS390XState *env, uint32_t code);

void probe_write_access(CPUS390XState *env, uint64_t addr, uint64_t len,
                        uintptr_t ra);

/* translate.c */
void s390x_translate_init(void);
void s390x_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc);
void s390x_restore_state_to_opc(CPUState *cs,
                                const TranslationBlock *tb,
                                const uint64_t *data);

#endif /* S390X_INTERNAL_H */
