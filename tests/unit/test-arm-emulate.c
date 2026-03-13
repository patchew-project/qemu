/*
 * Unit tests for AArch64 ISV=0 instruction emulation library
 *
 * Copyright (c) 2026 Lucas Amaral <lucaaamaral@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "arm_emulate.h"

/* Mock environment: GPR, FPR, and flat memory */

typedef struct MockEnv {
    uint64_t gpr[32];          /* X0-X30, X31=SP */
    uint8_t  fpr[32][16];      /* V0-V31, 128 bits each */
    uint8_t  mem[0x1000];      /* 4 KiB flat address space */
    bool     mem_fail;         /* if true, memory ops return -1 */
} MockEnv;

static MockEnv *env_from_cpu(CPUState *cpu)
{
    return (MockEnv *)cpu;
}

static uint64_t mock_read_gpr(CPUState *cpu, int reg)
{
    return env_from_cpu(cpu)->gpr[reg];
}

static void mock_write_gpr(CPUState *cpu, int reg, uint64_t val)
{
    env_from_cpu(cpu)->gpr[reg] = val;
}

static void mock_read_fpreg(CPUState *cpu, int reg, void *buf, int size)
{
    memcpy(buf, env_from_cpu(cpu)->fpr[reg], size);
}

static void mock_write_fpreg(CPUState *cpu, int reg, const void *buf, int size)
{
    MockEnv *env = env_from_cpu(cpu);
    memset(env->fpr[reg], 0, 16);
    memcpy(env->fpr[reg], buf, size);
}

static int mock_read_mem(CPUState *cpu, uint64_t va, void *buf, int size)
{
    MockEnv *env = env_from_cpu(cpu);
    if (env->mem_fail || va + size > sizeof(env->mem)) {
        return -1;
    }
    memcpy(buf, env->mem + va, size);
    return 0;
}

static int mock_write_mem(CPUState *cpu, uint64_t va, const void *buf, int size)
{
    MockEnv *env = env_from_cpu(cpu);
    if (env->mem_fail || va + size > sizeof(env->mem)) {
        return -1;
    }
    memcpy(env->mem + va, buf, size);
    return 0;
}

static const struct arm_emul_ops mock_ops = {
    .read_gpr    = mock_read_gpr,
    .write_gpr   = mock_write_gpr,
    .read_fpreg  = mock_read_fpreg,
    .write_fpreg = mock_write_fpreg,
    .read_mem    = mock_read_mem,
    .write_mem   = mock_write_mem,
};

/* Helper: reset mock environment */
static MockEnv *fresh_env(MockEnv *env)
{
    memset(env, 0, sizeof(*env));
    return env;
}

/* Helper: call arm_emul_insn with the mock environment */
static ArmEmulResult emul(MockEnv *env, uint32_t insn)
{
    return arm_emul_insn((CPUState *)env, &mock_ops, insn);
}

/* Helper: write a uint64_t to mock memory at a given VA */
static void mem_write64(MockEnv *env, uint64_t va, uint64_t val)
{
    memcpy(env->mem + va, &val, 8);
}

/* Helper: read a uint64_t from mock memory */
static uint64_t mem_read64(MockEnv *env, uint64_t va)
{
    uint64_t val = 0;
    memcpy(&val, env->mem + va, 8);
    return val;
}

/* Helper: read a uint32_t from mock memory */
static uint32_t mem_read32(MockEnv *env, uint64_t va)
{
    uint32_t val = 0;
    memcpy(&val, env->mem + va, 4);
    return val;
}

/* STP / LDP (64-bit store/load pair, signed offset) */

/*
 * STP X0, X1, [X2]
 *   10 101 0 010 0 0000000 00001 00010 00000
 *   = 0xA9000440
 */
static void test_stp_offset(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 0xDEADBEEF;
    env.gpr[1] = 0xCAFEBABE;
    env.gpr[2] = 0x100;  /* base address */

    g_assert_cmpint(emul(&env, 0xA9000440), ==, ARM_EMUL_OK);

    g_assert_cmphex(mem_read64(&env, 0x100), ==, 0xDEADBEEF);
    g_assert_cmphex(mem_read64(&env, 0x108), ==, 0xCAFEBABE);
    /* No writeback — base unchanged */
    g_assert_cmphex(env.gpr[2], ==, 0x100);
}

/*
 * LDP X3, X4, [X5]
 *   10 101 0 010 1 0000000 00100 00101 00011
 *   = 0xA94010A3
 */
static void test_ldp_offset(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[5] = 0x200;
    mem_write64(&env, 0x200, 0x1111111111111111ULL);
    mem_write64(&env, 0x208, 0x2222222222222222ULL);

    g_assert_cmpint(emul(&env, 0xA94010A3), ==, ARM_EMUL_OK);

    g_assert_cmphex(env.gpr[3], ==, 0x1111111111111111ULL);
    g_assert_cmphex(env.gpr[4], ==, 0x2222222222222222ULL);
}

/* STP pre-indexed (writeback) */

/*
 * STP X0, X1, [X2, #16]!
 *   10 101 0 011 0 0000010 00001 00010 00000
 *   = 0xA9810440
 *   imm7=+2, scaled by 8 = offset +16
 */
static void test_stp_preindex(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 0xAAAA;
    env.gpr[1] = 0xBBBB;
    env.gpr[2] = 0x100;

    g_assert_cmpint(emul(&env, 0xA9810440), ==, ARM_EMUL_OK);

    /* Data stored at base+16 = 0x110 */
    g_assert_cmphex(mem_read64(&env, 0x110), ==, 0xAAAA);
    g_assert_cmphex(mem_read64(&env, 0x118), ==, 0xBBBB);
    /* Writeback: base updated to base+16 */
    g_assert_cmphex(env.gpr[2], ==, 0x110);
}

/* STR / LDR unsigned offset (64-bit) */

/*
 * STR X0, [X1]
 *   11 111 0 01 00 000000000000 00001 00000
 *   = 0xF9000020
 */
static void test_str_uoffset(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 0x42;
    env.gpr[1] = 0x80;

    g_assert_cmpint(emul(&env, 0xF9000020), ==, ARM_EMUL_OK);
    g_assert_cmphex(mem_read64(&env, 0x80), ==, 0x42);
}

/*
 * LDR X2, [X1]
 *   11 111 0 01 01 000000000000 00001 00010
 *   = 0xF9400022
 */
static void test_ldr_uoffset(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[1] = 0x80;
    mem_write64(&env, 0x80, 0xFEDCBA9876543210ULL);

    g_assert_cmpint(emul(&env, 0xF9400022), ==, ARM_EMUL_OK);
    g_assert_cmphex(env.gpr[2], ==, 0xFEDCBA9876543210ULL);
}

/* LDRB zero-extend / LDRSB sign-extend */

/*
 * LDRB W2, [X1]  (zero-extend byte to 32-bit)
 *   00 111 0 01 01 000000000000 00001 00010
 *   = 0x39400022
 */
static void test_ldrb_zero_extend(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[1] = 0x80;
    env.mem[0x80] = 0xFF;

    g_assert_cmpint(emul(&env, 0x39400022), ==, ARM_EMUL_OK);
    g_assert_cmphex(env.gpr[2], ==, 0xFF);
}

/*
 * LDRSB X2, [X1]  (sign-extend byte to 64-bit)
 *   00 111 0 01 10 000000000000 00001 00010
 *   = 0x39800022
 */
static void test_ldrsb_sign_extend(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[1] = 0x80;
    env.mem[0x80] = 0x80;  /* -128 as signed byte */

    g_assert_cmpint(emul(&env, 0x39800022), ==, ARM_EMUL_OK);
    g_assert_cmphex(env.gpr[2], ==, 0xFFFFFFFFFFFFFF80ULL);
}

/* XZR -- register 31 reads as zero for GPR data */

/*
 * STR XZR, [X1]  (store zero register)
 *   11 111 0 01 00 000000000000 00001 11111
 *   = 0xF900003F
 */
static void test_xzr_reads_zero(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[31] = 0x9999;  /* SP value — should NOT be stored */
    env.gpr[1] = 0x80;
    mem_write64(&env, 0x80, 0xFFFF);  /* pre-fill to verify overwrite */

    g_assert_cmpint(emul(&env, 0xF900003F), ==, ARM_EMUL_OK);
    /* XZR is zero, not SP */
    g_assert_cmphex(mem_read64(&env, 0x80), ==, 0);
}

/* Atomic -- LDADD */

/*
 * LDADD X0, X1, [X2]
 *   11 111 0 00 00 1 00000 0000 00 00010 00001
 *   = 0xF8200041
 *   sz=3, a=0, r=0, rs=0, opc=0000 (ADD), rn=2, rt=1
 */
static void test_ldadd(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 5;       /* operand (Rs) */
    env.gpr[2] = 0x100;   /* base address */
    mem_write64(&env, 0x100, 10);  /* old value */

    g_assert_cmpint(emul(&env, 0xF8200041), ==, ARM_EMUL_OK);

    /* Rt gets old value */
    g_assert_cmphex(env.gpr[1], ==, 10);
    /* Memory gets old + operand */
    g_assert_cmphex(mem_read64(&env, 0x100), ==, 15);
}

/* SWP */

/*
 * SWP X0, X1, [X2]
 *   11 111 0 00 00 1 00000 1000 00 00010 00001
 *   = 0xF8208041
 */
static void test_swp(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 42;      /* new value (Rs) */
    env.gpr[2] = 0x100;   /* base address */
    mem_write64(&env, 0x100, 99);  /* old value */

    g_assert_cmpint(emul(&env, 0xF8208041), ==, ARM_EMUL_OK);

    /* Rt gets old value */
    g_assert_cmphex(env.gpr[1], ==, 99);
    /* Memory gets new value */
    g_assert_cmphex(mem_read64(&env, 0x100), ==, 42);
}

/* CAS (compare-and-swap, 64-bit) */

/*
 * CAS X0, X2, [X4]
 *   11 001000 1 0 1 00000 0 11111 00100 00010
 *   = 0xC8A07C82
 *   sz=3, a=0, r=0, rs=0 (compare), rt=2 (new), rn=4 (base)
 */
static void test_cas_match(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 100;     /* Rs: compare value */
    env.gpr[2] = 200;     /* Rt: new value */
    env.gpr[4] = 0x100;   /* Rn: base address */
    mem_write64(&env, 0x100, 100);  /* memory == compare, swap occurs */

    g_assert_cmpint(emul(&env, 0xC8A07C82), ==, ARM_EMUL_OK);

    /* Rs gets old memory value */
    g_assert_cmphex(env.gpr[0], ==, 100);
    /* Memory updated to new value */
    g_assert_cmphex(mem_read64(&env, 0x100), ==, 200);
}

/* CAS with no match — memory unchanged */
static void test_cas_nomatch(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 100;     /* compare */
    env.gpr[2] = 200;     /* new */
    env.gpr[4] = 0x100;
    mem_write64(&env, 0x100, 999);  /* memory != compare, no swap */

    g_assert_cmpint(emul(&env, 0xC8A07C82), ==, ARM_EMUL_OK);

    g_assert_cmphex(env.gpr[0], ==, 999);  /* Rs gets old value */
    g_assert_cmphex(mem_read64(&env, 0x100), ==, 999);  /* unchanged */
}

/* CASP (pair compare-and-swap) */

/*
 * CASP X0, X1, X2, X3, [X4]  (64-bit pair)
 *   01 001000 0 0 1 00000 0 11111 00100 00010
 *   = 0x48207C82
 */
static void test_casp_match(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[0] = 0xAA;  env.gpr[1] = 0xBB;  /* Rs pair: compare */
    env.gpr[2] = 0xCC;  env.gpr[3] = 0xDD;  /* Rt pair: new */
    env.gpr[4] = 0x100;
    mem_write64(&env, 0x100, 0xAA);
    mem_write64(&env, 0x108, 0xBB);

    g_assert_cmpint(emul(&env, 0x48207C82), ==, ARM_EMUL_OK);

    g_assert_cmphex(mem_read64(&env, 0x100), ==, 0xCC);
    g_assert_cmphex(mem_read64(&env, 0x108), ==, 0xDD);
}

/* CASP with odd register -- validation rejects */

/*
 * CASP with Rs=X1 (odd) -- trans_CASP returns false
 *   01 001000 0 0 1 00001 0 11111 00100 00010
 *   = 0x48217C82
 */
static void test_casp_odd_register(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[4] = 0x100;

    /* Odd Rs -- decoder returns false -- ARM_EMUL_UNHANDLED */
    g_assert_cmpint(emul(&env, 0x48217C82), ==, ARM_EMUL_UNHANDLED);
}

/* Unrecognized instruction -- ARM_EMUL_UNHANDLED */

static void test_unhandled(void)
{
    MockEnv env;
    fresh_env(&env);

    /* B #0 = 0x14000000 — a branch, not a load/store */
    g_assert_cmpint(emul(&env, 0x14000000), ==, ARM_EMUL_UNHANDLED);
}

/* Memory error -- ARM_EMUL_ERR_MEM */

static void test_mem_error(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[1] = 0x80;
    env.mem_fail = true;

    /* LDR X2, [X1] — memory read will fail */
    g_assert_cmpint(emul(&env, 0xF9400022), ==, ARM_EMUL_ERR_MEM);
}

/* PRFM (prefetch) -- NOP, returns OK */

/*
 * PRFM #0, [X1]  (unsigned offset, imm=0)
 *   11 111 0 01 10 000000000000 00001 00000
 *   = 0xF9800020
 */
static void test_prfm_nop(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[1] = 0x80;

    g_assert_cmpint(emul(&env, 0xF9800020), ==, ARM_EMUL_OK);
    /* No state change — it's a NOP */
}

/* SIMD/FP store/load pair */

/*
 * STP S0, S1, [X2]  (32-bit FP pair, signed offset, imm=0)
 *   00 101 1 010 0 0000000 00001 00010 00000
 *   = 0x2D000440
 */
static void test_stp_fp(void)
{
    MockEnv env;
    fresh_env(&env);

    uint32_t f0 = 0x3F800000;  /* 1.0f */
    uint32_t f1 = 0x40000000;  /* 2.0f */
    memcpy(env.fpr[0], &f0, 4);
    memcpy(env.fpr[1], &f1, 4);
    env.gpr[2] = 0x200;

    g_assert_cmpint(emul(&env, 0x2D000440), ==, ARM_EMUL_OK);

    g_assert_cmphex(mem_read32(&env, 0x200), ==, 0x3F800000);
    g_assert_cmphex(mem_read32(&env, 0x204), ==, 0x40000000);
}

/* LDR post-indexed (writeback after load) */

/*
 * LDR X3, [X1], #8  (post-indexed, imm=+8)
 *   11 111 0 00 01 0 000001000 01 00001 00011
 *   = 0xF8408423
 *   sz=3, opc=01, imm9=+8, type=01 (post-index), rn=1, rt=3
 */
static void test_ldr_postindex(void)
{
    MockEnv env;
    fresh_env(&env);

    env.gpr[1] = 0x100;
    mem_write64(&env, 0x100, 0x55AA55AA55AA55AAULL);

    g_assert_cmpint(emul(&env, 0xF8408423), ==, ARM_EMUL_OK);

    /* Load from original base */
    g_assert_cmphex(env.gpr[3], ==, 0x55AA55AA55AA55AAULL);
    /* Writeback: base += 8 */
    g_assert_cmphex(env.gpr[1], ==, 0x108);
}

/* Entry point */

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* Load/store pair */
    g_test_add_func("/arm-emulate/stp-offset", test_stp_offset);
    g_test_add_func("/arm-emulate/ldp-offset", test_ldp_offset);
    g_test_add_func("/arm-emulate/stp-preindex", test_stp_preindex);
    g_test_add_func("/arm-emulate/stp-fp", test_stp_fp);

    /* Load/store single */
    g_test_add_func("/arm-emulate/str-uoffset", test_str_uoffset);
    g_test_add_func("/arm-emulate/ldr-uoffset", test_ldr_uoffset);
    g_test_add_func("/arm-emulate/ldr-postindex", test_ldr_postindex);
    g_test_add_func("/arm-emulate/ldrb-zero-extend", test_ldrb_zero_extend);
    g_test_add_func("/arm-emulate/ldrsb-sign-extend", test_ldrsb_sign_extend);

    /* XZR */
    g_test_add_func("/arm-emulate/xzr-reads-zero", test_xzr_reads_zero);

    /* Atomics */
    g_test_add_func("/arm-emulate/ldadd", test_ldadd);
    g_test_add_func("/arm-emulate/swp", test_swp);

    /* Compare-and-swap */
    g_test_add_func("/arm-emulate/cas-match", test_cas_match);
    g_test_add_func("/arm-emulate/cas-nomatch", test_cas_nomatch);
    g_test_add_func("/arm-emulate/casp-match", test_casp_match);
    g_test_add_func("/arm-emulate/casp-odd-register", test_casp_odd_register);

    /* NOP */
    g_test_add_func("/arm-emulate/prfm-nop", test_prfm_nop);

    /* Error handling */
    g_test_add_func("/arm-emulate/unhandled", test_unhandled);
    g_test_add_func("/arm-emulate/mem-error", test_mem_error);

    return g_test_run();
}
