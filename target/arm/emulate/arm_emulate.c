/*
 * AArch64 instruction emulation for ISV=0 data aborts
 *
 * Copyright (c) 2026 Lucas Amaral <lucaaamaral@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "arm_emulate.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"
#include "exec/cpu-common.h"
#include "system/memory.h"
#include "exec/target_page.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"

/* Named "DisasContext" as required by the decodetree code generator */
typedef struct {
    CPUState *cpu;
    CPUARMState *env;
    ArmEmulResult result;
    bool be_data;
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

/*
 * Memory access via guest MMU translation.
 *
 * Translates the virtual address through the guest page tables using
 * get_phys_addr(), then performs the access on the resulting physical
 * address via address_space_read/write().  Each page-sized chunk is
 * translated independently, so accesses that span a page boundary
 * are handled correctly even when the pages map to different physical
 * addresses.
 */

static int mem_access(DisasContext *ctx, uint64_t va, void *buf, int size,
                      MMUAccessType access_type)
{
    ARMMMUIdx mmu_idx = arm_mmu_idx(ctx->env);

    while (size > 0) {
        int chunk = MIN(size, TARGET_PAGE_SIZE - (va & ~TARGET_PAGE_MASK));
        GetPhysAddrResult res = {};
        ARMMMUFaultInfo fi = {};

        if (get_phys_addr(ctx->env, va, access_type, 0, mmu_idx,
                          &res, &fi)) {
            ctx->result = ARM_EMUL_ERR_MEM;
            return -1;
        }

        AddressSpace *as = arm_addressspace(ctx->cpu, res.f.attrs);
        MemTxResult txr;

        if (access_type == MMU_DATA_STORE) {
            txr = address_space_write(as, res.f.phys_addr, res.f.attrs,
                                      buf, chunk);
        } else {
            txr = address_space_read(as, res.f.phys_addr, res.f.attrs,
                                     buf, chunk);
        }

        if (txr != MEMTX_OK) {
            ctx->result = ARM_EMUL_ERR_MEM;
            return -1;
        }

        va += chunk;
        buf += chunk;
        size -= chunk;
    }
    return 0;
}

static int mem_read(DisasContext *ctx, uint64_t va, void *buf, int size)
{
    return mem_access(ctx, va, buf, size, MMU_DATA_LOAD);
}

static int mem_write(DisasContext *ctx, uint64_t va, const void *buf, int size)
{
    return mem_access(ctx, va, (void *)buf, size, MMU_DATA_STORE);
}

/*
 * Endian-aware GPR <-> memory buffer helpers.
 *
 * mem_read/mem_write transfer raw bytes between guest VA and a host buffer.
 * mem_ld/mem_st convert between a uint64_t register value and the guest
 * byte order in a memory buffer.
 */

static uint64_t mem_ld(DisasContext *ctx, const void *buf, int size)
{
    return ctx->be_data ? ldn_be_p(buf, size) : ldn_le_p(buf, size);
}

static void mem_st(DisasContext *ctx, void *buf, int size, uint64_t val)
{
    if (ctx->be_data) {
        stn_be_p(buf, size, val);
    } else {
        stn_le_p(buf, size, val);
    }
}

/* Apply sign/zero extension */
static uint64_t load_extend(uint64_t val, int sz, int sign, int ext)
{
    int data_bits = 8 << sz;

    if (sign) {
        val = sextract64(val, 0, data_bits);
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

/* Load/store single -- immediate (GPR) (DDI 0487 C3.3.8 -- C3.3.13) */

static bool trans_STR_i(DisasContext *ctx, arg_ldst_imm *a)
{
    int esize = (a->sz <= 3) ? (1 << a->sz) : 16;
    int64_t offset = a->u ? ((int64_t)(uint64_t)a->imm << a->sz)
                          : (int64_t)a->imm;
    uint64_t base = base_read(ctx, a->rn);
    uint64_t va = a->p ? base : base + offset;

    uint8_t buf[16];
    uint64_t val = gpr_read(ctx, a->rt);
    mem_st(ctx, buf, esize, val);
    if (mem_write(ctx, va, buf, esize) != 0) {
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
    uint8_t buf[16];

    if (mem_read(ctx, va, buf, esize) != 0) {
        return true;
    }

    uint64_t val = mem_ld(ctx, buf, esize);
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

    uint8_t buf[16];
    uint64_t val = gpr_read(ctx, a->rt);
    mem_st(ctx, buf, esize, val);
    mem_write(ctx, va, buf, esize);
    return true;
}

static bool trans_LDR(DisasContext *ctx, arg_ldst *a)
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

    uint64_t val = mem_ld(ctx, buf, esize);
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

/* PRFM, DC cache maintenance -- treated as NOP */
static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    return true;
}

/* Entry point */

ArmEmulResult arm_emul_insn(CPUArchState *env, uint32_t insn)
{
    DisasContext ctx = {
        .cpu = env_cpu(env),
        .env = env,
        .result = ARM_EMUL_OK,
        .be_data = arm_cpu_data_is_big_endian(env),
    };

    if (!decode_a64_ldst(&ctx, insn)) {
        return ARM_EMUL_UNHANDLED;
    }

    return ctx.result;
}
