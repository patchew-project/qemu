/*
 * QEMU LowRisc Ibex core features
 *
 * Copyright (c) 2023 Rivos, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"

/* Custom CSRs */
#define CSR_CPUCTRLSTS 0x7c0
#define CSR_SECURESEED 0x7c1

#define CPUCTRLSTS_ICACHE_ENABLE     0x000
#define CPUCTRLSTS_DATA_IND_TIMING   0x001
#define CPUCTRLSTS_DUMMY_INSTR_EN    0x002
#define CPUCTRLSTS_DUMMY_INSTR_MASK  0x038
#define CPUCTRLSTS_SYNC_EXC_SEEN     0x040
#define CPUCTRLSTS_DOUBLE_FAULT_SEEN 0x080
#define CPUCTRLSTS_IC_SCR_KEY_VALID  0x100

#if !defined(CONFIG_USER_ONLY)

static RISCVException read_cpuctrlsts(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = CPUCTRLSTS_IC_SCR_KEY_VALID | env->cpuctrlsts;
    return RISCV_EXCP_NONE;
}

static RISCVException write_cpuctrlsts(CPURISCVState *env, int csrno,
                                       target_ulong val, uintptr_t a)
{
    /* b7 can only be cleared */
    env->cpuctrlsts &= ~0xbf;
    /* b6 should be cleared on mret */
    env->cpuctrlsts |= val & 0x3f;
    return RISCV_EXCP_NONE;
}

static RISCVException read_secureseed(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    /*
     * "Seed values are not actually stored in a register and so reads to this
     * register will always return zero."
     */
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException write_secureseed(CPURISCVState *env, int csrno,
                                       target_ulong val, uintptr_t a)
{
    (void)val;
    return RISCV_EXCP_NONE;
}

static RISCVException any(CPURISCVState *env, int csrno)
{
    /*
     *  unfortunately, this predicate is not public, so duplicate the standard
     *  implementation
     */
    return RISCV_EXCP_NONE;
}

const RISCVCSR ibex_csr_list[] = {
    {
        .csrno = CSR_CPUCTRLSTS,
        .csr_ops = { "cpuctrlsts", any, &read_cpuctrlsts, &write_cpuctrlsts },
    },
    {
        .csrno = CSR_SECURESEED,
        .csr_ops = { "secureseed", any, &read_secureseed, &write_secureseed },
    },
    {}
};

#endif /* !defined(CONFIG_USER_ONLY) */

