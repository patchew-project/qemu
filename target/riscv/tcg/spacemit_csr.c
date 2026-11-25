/*
 * Spacemit X100 CSRs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_vendorid.h"
#include "csr.h"

#define CSR_SPACEMIT_TCMCFG   0x5db
#define CSR_SPACEMIT_MSETUP   0x7c0
#define CSR_SPACEMIT_MHCR     0x7c1
#define CSR_SPACEMIT_MHCOR    0x7c2
#define CSR_SPACEMIT_PMACFG0  0x7de
#define CSR_SPACEMIT_PMAADDR8 0x7e8
#define CSR_SPACEMIT_ML2SETUP 0x7f0
#define CSR_SPACEMIT_ML2HINT  0x7f7
#define CSR_SPACEMIT_MVMD1    0xbf9

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

static RISCVException any_x100(CPURISCVState *env, int csrno)
{
    if (env_archcpu(env)->cfg.mimpid != SPACEMIT_X100_MIMPID ||
      env_archcpu(env)->cfg.marchid != SPACEMIT_X100_MARCHID) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return RISCV_EXCP_NONE;
}

static bool test_spacemit_mvendorid(RISCVCPU *cpu)
{
    return cpu->cfg.mvendorid == SPACEMIT_VENDOR_ID;
}

const RISCVCSR spacemit_csr_list[] = {
    { .csrno = CSR_SPACEMIT_TCMCFG,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "tcmcfg", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_MSETUP,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "msetup", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_MHCR,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "mhcr", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_MHCOR,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "mhcor", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_PMACFG0,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "pmacfg0", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_PMAADDR8,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "pmaaddr8", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_ML2SETUP,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "ml2setup", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_ML2HINT,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "ml2hint", any_x100, read_zero, write_none } },
    { .csrno = CSR_SPACEMIT_MVMD1,
      .insertion_test = test_spacemit_mvendorid,
      .csr_ops = { "mvmd1", any_x100, read_zero, write_none } },
    { }
};
