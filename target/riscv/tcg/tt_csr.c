/*
 * Tenstorrent CPU implementation specifics.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Tenstorrent
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "target/riscv/tcg/csr.h"

/* RCPU custom CSRs */
#define RCPU_CSR_MICACHE          0x7c0
#define RCPU_CSR_MDCACHE          0x7c1
#define RCPU_CSR_MITCMCR          0x7c5
#define RCPU_CSR_MDTCMCR          0x7c6
#define RCPU_CSR_MHCR             0x7c7
#define RCPU_CSR_MEXINFO          0x7ca
#define RCPU_CSR_MNMIE            0xbc6

#define RCPU_MARCH_ID             0x80000171

/* predicate for an RCPU core */
static RISCVException rcpu(CPURISCVState *env, int csrno)
{
    if (env_archcpu(env)->cfg.marchid != RCPU_MARCH_ID) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_zero(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = 0;

    return RISCV_EXCP_NONE;
}

static RISCVException write_none(CPURISCVState *env, int csrno,
                                 target_ulong val, uintptr_t ra)
{
    return RISCV_EXCP_NONE;
}

const RISCVCSR tt_csr_list[] = {
    { .csrno = RCPU_CSR_MICACHE,
      .csr_ops = { "micache", rcpu, read_zero, write_none} },
    { .csrno = RCPU_CSR_MDCACHE,
      .csr_ops = { "mdcache", rcpu, read_zero, write_none} },
    { .csrno = RCPU_CSR_MITCMCR,
      .csr_ops = { "mitcmcr", rcpu, read_zero, write_none} },
    { .csrno = RCPU_CSR_MDTCMCR,
      .csr_ops = { "mdtcmcr", rcpu, read_zero, write_none} },
    { .csrno = RCPU_CSR_MHCR,
      .csr_ops = { "mhcr", rcpu, read_zero, write_none} },
    { .csrno = RCPU_CSR_MEXINFO,
      .csr_ops = { "mexinfo", rcpu, read_zero, write_none} },
    { .csrno = RCPU_CSR_MNMIE,
       .csr_ops = { "mnmie", rcpu, read_zero, write_none} },
    { },
};
