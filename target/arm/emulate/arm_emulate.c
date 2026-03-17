/*
 * AArch64 instruction emulation for ISV=0 data aborts
 *
 * Copyright (c) 2026 Lucas Amaral <lucaaamaral@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "arm_emulate.h"
#include "target/arm/cpu.h"
#include "exec/cpu-common.h"
#include "exec/target_page.h"

/* TODO: assumes LE guest data layout (sufficient for HVF/WHPX, both LE-only) */

/* Named "DisasContext" as required by the decodetree code generator */
typedef struct {
    CPUState *cpu;
    CPUARMState *env;
    ArmEmulResult result;
} DisasContext;

#include "decode-a64-ldst.c.inc"

/* GPR data access (Rt, Rs, Rt2) -- register 31 = XZR */

static uint64_t gpr_read(DisasContext *ctx, int reg)
{
    if (reg == 31) {
        return 0;  /* XZR */
    }
    return ctx->env->xregs[reg];
}

static void gpr_write(DisasContext *ctx, int reg, uint64_t val)
{
    if (reg == 31) {
        return;  /* XZR -- discard */
    }
    ctx->env->xregs[reg] = val;
    ctx->cpu->vcpu_dirty = true;
}

/* Base register access (Rn) -- register 31 = SP */

static uint64_t base_read(DisasContext *ctx, int rn)
{
    return ctx->env->xregs[rn];
}

static void base_write(DisasContext *ctx, int rn, uint64_t val)
{
    ctx->env->xregs[rn] = val;
    ctx->cpu->vcpu_dirty = true;
}

/* SIMD/FP register access */

static void fpreg_read(DisasContext *ctx, int reg, void *buf, int size)
{
    memcpy(buf, &ctx->env->vfp.zregs[reg], size);
}

static void fpreg_write(DisasContext *ctx, int reg, const void *buf, int size)
{
    memset(&ctx->env->vfp.zregs[reg], 0, sizeof(ctx->env->vfp.zregs[reg]));
    memcpy(&ctx->env->vfp.zregs[reg], buf, size);
    ctx->cpu->vcpu_dirty = true;
}

/* Memory access wrappers */

static int mem_read(DisasContext *ctx, uint64_t va, void *buf, int size)
{
    if (((va & ~TARGET_PAGE_MASK) + size) > TARGET_PAGE_SIZE) {
        ctx->result = ARM_EMUL_ERR_MEM;
        return -1;
    }
    int ret = cpu_memory_rw_debug(ctx->cpu, va, buf, size, false);
    if (ret != 0) {
        ctx->result = ARM_EMUL_ERR_MEM;
    }
    return ret;
}

static int mem_write(DisasContext *ctx, uint64_t va, const void *buf, int size)
{
    if (((va & ~TARGET_PAGE_MASK) + size) > TARGET_PAGE_SIZE) {
        ctx->result = ARM_EMUL_ERR_MEM;
        return -1;
    }
    int ret = cpu_memory_rw_debug(ctx->cpu, va, (void *)buf, size, true);
    if (ret != 0) {
        ctx->result = ARM_EMUL_ERR_MEM;
    }
    return ret;
}

/* Sign/zero extension helpers */

static uint64_t sign_extend(uint64_t val, int from_bits)
{
    int shift = 64 - from_bits;
    return (int64_t)(val << shift) >> shift;
}

/* Apply sign/zero extension */
static uint64_t load_extend(uint64_t val, int sz, int sign, int ext)
{
    int data_bits = 8 << sz;

    if (sign) {
        val = sign_extend(val, data_bits);
        if (ext) {
            /* Sign-extend to 32 bits (W register) */
            val &= 0xFFFFFFFF;
        }
    } else if (ext) {
        /* Zero-extend to 32 bits (W register) */
        val &= 0xFFFFFFFF;
    }
    return val;
}

/*
 * Load/store pair: STP, LDP, STNP, LDNP, STGP, LDPSW
 * (DDI 0487 C3.3.14 -- C3.3.16)
 */

static bool trans_STP(DisasContext *ctx, arg_ldstpair *a)
{
    int esize = 1 << a->sz;                   /* 4 or 8 bytes */
    int64_t offset = (int64_t)a->imm << a->sz;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset; /* post-index: unmodified base */
    uint8_t buf[16];                           /* max 2 x 8 bytes */

    uint64_t v1 = gpr_read(ctx, a->rt);
    uint64_t v2 = gpr_read(ctx, a->rt2);
    memcpy(buf, &v1, esize);
    memcpy(buf + esize, &v2, esize);

    if (mem_write(ctx, va, buf, 2 * esize) != 0) {
        return true;
    }

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

static bool trans_LDP(DisasContext *ctx, arg_ldstpair *a)
{
    int esize = 1 << a->sz;
    int64_t offset = (int64_t)a->imm << a->sz;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;
    uint8_t buf[16];
    uint64_t v1 = 0, v2 = 0;

    if (mem_read(ctx, va, buf, 2 * esize) != 0) {
        return true;
    }
    memcpy(&v1, buf, esize);
    memcpy(&v2, buf + esize, esize);

    /* LDPSW: sign-extend 32-bit values to 64-bit (sign=1, sz=2) */
    if (a->sign) {
        v1 = sign_extend(v1, 8 * esize);
        v2 = sign_extend(v2, 8 * esize);
    }

    gpr_write(ctx, a->rt, v1);
    gpr_write(ctx, a->rt2, v2);

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

/* STGP: tag operation is a NOP for emulation; data stored via STP */
static bool trans_STGP(DisasContext *ctx, arg_ldstpair *a)
{
    return trans_STP(ctx, a);
}

/*
 * SIMD/FP load/store pair: STP_v, LDP_v
 * (DDI 0487 C3.3.14 -- C3.3.16)
 */

static bool trans_STP_v(DisasContext *ctx, arg_ldstpair *a)
{
    int esize = 1 << a->sz;                   /* 4, 8, or 16 bytes */
    int64_t offset = (int64_t)a->imm << a->sz;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;
    uint8_t buf[32];                           /* max 2 x 16 bytes */

    fpreg_read(ctx, a->rt, buf, esize);
    fpreg_read(ctx, a->rt2, buf + esize, esize);

    if (mem_write(ctx, va, buf, 2 * esize) != 0) {
        return true;
    }

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

static bool trans_LDP_v(DisasContext *ctx, arg_ldstpair *a)
{
    int esize = 1 << a->sz;
    int64_t offset = (int64_t)a->imm << a->sz;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;
    uint8_t buf[32];

    if (mem_read(ctx, va, buf, 2 * esize) != 0) {
        return true;
    }

    fpreg_write(ctx, a->rt, buf, esize);
    fpreg_write(ctx, a->rt2, buf + esize, esize);

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

/* Load/store single -- immediate (GPR) (DDI 0487 C3.3.8 -- C3.3.13) */

static bool trans_STR_i(DisasContext *ctx, arg_ldst_imm *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int64_t offset = a->u ? ((int64_t)(uint64_t)a->imm << a->sz)
                          : (int64_t)a->imm;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;

    uint64_t val = gpr_read(ctx, a->rt);
    if (mem_write(ctx, va, &val, esize) != 0) {
        return true;
    }

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

static bool trans_LDR_i(DisasContext *ctx, arg_ldst_imm *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int64_t offset = a->u ? ((int64_t)(uint64_t)a->imm << a->sz)
                          : (int64_t)a->imm;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;
    uint64_t val = 0;

    if (mem_read(ctx, va, &val, esize) != 0) {
        return true;
    }

    val = load_extend(val, a->sz, a->sign, a->ext);
    gpr_write(ctx, a->rt, val);

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

/*
 * Load/store single -- immediate (SIMD/FP)
 * STR_v_i / LDR_v_i (DDI 0487 C3.3.10)
 */

static bool trans_STR_v_i(DisasContext *ctx, arg_ldst_imm *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int64_t offset = a->u ? ((int64_t)(uint64_t)a->imm << a->sz)
                          : (int64_t)a->imm;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;
    uint8_t buf[16];

    fpreg_read(ctx, a->rt, buf, esize);
    if (mem_write(ctx, va, buf, esize) != 0) {
        return true;
    }

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

static bool trans_LDR_v_i(DisasContext *ctx, arg_ldst_imm *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int64_t offset = a->u ? ((int64_t)(uint64_t)a->imm << a->sz)
                          : (int64_t)a->imm;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;
    uint8_t buf[16];

    if (mem_read(ctx, va, buf, esize) != 0) {
        return true;
    }

    fpreg_write(ctx, a->rt, buf, esize);

    if (a->w) {
        base_write(ctx, a->rn, base + offset);
    }
    return true;
}

/* Register offset extension (DDI 0487 C6.2.131) */

static uint64_t extend_reg(uint64_t val, int option, int shift)
{
    switch (option) {
    case 0: /* UXTB */
        val = (uint8_t)val;
        break;
    case 1: /* UXTH */
        val = (uint16_t)val;
        break;
    case 2: /* UXTW */
        val = (uint32_t)val;
        break;
    case 3: /* UXTX / LSL */
        break;
    case 4: /* SXTB */
        val = (int64_t)(int8_t)val;
        break;
    case 5: /* SXTH */
        val = (int64_t)(int16_t)val;
        break;
    case 6: /* SXTW */
        val = (int64_t)(int32_t)val;
        break;
    case 7: /* SXTX */
        break;
    }
    return val << shift;
}

/*
 * Load/store single -- register offset (GPR)
 * STR / LDR (DDI 0487 C3.3.9)
 */

static bool trans_STR(DisasContext *ctx, arg_ldst *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int shift = a->s ? a->sz : 0;
    uint64_t rm_val = gpr_read(ctx, a->rm);
    uint64_t offset = extend_reg(rm_val, a->opt, shift);
    uint64_t va = base_read(ctx, a->rn) + offset;

    uint64_t val = gpr_read(ctx, a->rt);
    mem_write(ctx, va, &val, esize);
    return true;
}

static bool trans_LDR(DisasContext *ctx, arg_ldst *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int shift = a->s ? a->sz : 0;
    uint64_t rm_val = gpr_read(ctx, a->rm);
    uint64_t offset = extend_reg(rm_val, a->opt, shift);
    uint64_t va = base_read(ctx, a->rn) + offset;
    uint64_t val = 0;

    if (mem_read(ctx, va, &val, esize) != 0) {
        return true;
    }

    val = load_extend(val, a->sz, a->sign, a->ext);
    gpr_write(ctx, a->rt, val);
    return true;
}

/*
 * Load/store single -- register offset (SIMD/FP)
 * STR_v / LDR_v (DDI 0487 C3.3.10)
 */

static bool trans_STR_v(DisasContext *ctx, arg_ldst *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int shift = a->s ? a->sz : 0;
    uint64_t rm_val = gpr_read(ctx, a->rm);
    uint64_t offset = extend_reg(rm_val, a->opt, shift);
    uint64_t va = base_read(ctx, a->rn) + offset;
    uint8_t buf[16];

    fpreg_read(ctx, a->rt, buf, esize);
    mem_write(ctx, va, buf, esize);
    return true;
}

static bool trans_LDR_v(DisasContext *ctx, arg_ldst *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int shift = a->s ? a->sz : 0;
    uint64_t rm_val = gpr_read(ctx, a->rm);
    uint64_t offset = extend_reg(rm_val, a->opt, shift);
    uint64_t va = base_read(ctx, a->rn) + offset;
    uint8_t buf[16];

    if (mem_read(ctx, va, buf, esize) != 0) {
        return true;
    }

    fpreg_write(ctx, a->rt, buf, esize);
    return true;
}

/*
 * Load/store exclusive: STXR, LDXR, STXP, LDXP
 * (DDI 0487 C3.3.6)
 *
 * Exclusive monitors have no meaning on MMIO.  STXR always reports
 * success (Rs=0) and LDXR does not set an exclusive monitor.
 */

static bool trans_STXR(DisasContext *ctx, arg_stxr *a)
{
    int esize = 1 << a->sz;
    uint64_t va = base_read(ctx, a->rn);
    uint64_t val = gpr_read(ctx, a->rt);

    if (mem_write(ctx, va, &val, esize) != 0) {
        return true;
    }

    /* Report success -- no exclusive monitor on emulated access */
    gpr_write(ctx, a->rs, 0);
    return true;
}

static bool trans_LDXR(DisasContext *ctx, arg_stxr *a)
{
    int esize = 1 << a->sz;
    uint64_t va = base_read(ctx, a->rn);
    uint64_t val = 0;

    if (mem_read(ctx, va, &val, esize) != 0) {
        return true;
    }

    gpr_write(ctx, a->rt, val);
    return true;
}

static bool trans_STXP(DisasContext *ctx, arg_stxr *a)
{
    int esize = 1 << a->sz;                   /* sz=2->4, sz=3->8 */
    uint64_t va = base_read(ctx, a->rn);
    uint8_t buf[16];

    uint64_t v1 = gpr_read(ctx, a->rt);
    uint64_t v2 = gpr_read(ctx, a->rt2);
    memcpy(buf, &v1, esize);
    memcpy(buf + esize, &v2, esize);

    if (mem_write(ctx, va, buf, 2 * esize) != 0) {
        return true;
    }

    gpr_write(ctx, a->rs, 0);  /* success */
    return true;
}

static bool trans_LDXP(DisasContext *ctx, arg_stxr *a)
{
    int esize = 1 << a->sz;
    uint64_t va = base_read(ctx, a->rn);
    uint8_t buf[16];
    uint64_t v1 = 0, v2 = 0;

    if (mem_read(ctx, va, buf, 2 * esize) != 0) {
        return true;
    }

    memcpy(&v1, buf, esize);
    memcpy(&v2, buf + esize, esize);
    gpr_write(ctx, a->rt, v1);
    gpr_write(ctx, a->rt2, v2);
    return true;
}

/* PRFM, DC cache maintenance -- treated as NOP */
static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    (void)ctx;
    (void)a;
    return true;
}

/* Entry point */

ArmEmulResult arm_emul_insn(CPUArchState *env, uint32_t insn)
{
    DisasContext ctx = {
        .cpu = env_cpu(env),
        .env = env,
        .result = ARM_EMUL_OK,
    };

    if (!decode_a64_ldst(&ctx, insn)) {
        return ARM_EMUL_UNHANDLED;
    }

    return ctx.result;
}
