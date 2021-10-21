/*
 * Copyright (c) 2021 Andes Technology Corp.
 * SPDX-License-Identifier: GPL-2.0+
 * Andes custom CSR table and handling functions
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "andes_cpu_bits.h"

struct andes_csr_val {
    target_long uitb;
};

#if !defined(CONFIG_USER_ONLY)
static RISCVException read_mmsc_cfg(CPURISCVState *env, int csrno, target_ulong *val)
{
    /* enable pma probe */
    *val = 0x40000000;
    return RISCV_EXCP_NONE;
}
#endif

static RISCVException write_uitb(CPURISCVState *env, int csrno, target_ulong val)
{
    struct andes_csr_val *andes_csr = env->custom_csr_val;
    andes_csr->uitb = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_uitb(CPURISCVState *env, int csrno, target_ulong *val)
{
    struct andes_csr_val *andes_csr = env->custom_csr_val;
    *val = andes_csr->uitb;
    return RISCV_EXCP_NONE;
}


static RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException read_zero(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException write_stub(CPURISCVState *env, int csrno, target_ulong val)
{
    return RISCV_EXCP_NONE;
}

int andes_custom_csr_size = sizeof(struct andes_csr_val);
riscv_custom_csr_operations andes_custom_csr_table[MAX_CUSTOM_CSR_NUM] = {
    /* ========= AndeStar V5 machine mode CSRs ========= */
    #if !defined(CONFIG_USER_ONLY)
    /* Configuration Registers */
    {CSR_MICM_CFG,         { "micm_cfg",          any, read_zero, write_stub} },
    {CSR_MDCM_CFG,         { "mdcm_cfg",          any, read_zero, write_stub} },
    {CSR_MMSC_CFG,         { "mmsc_cfg",          any, read_mmsc_cfg, write_stub} },
    {CSR_MMSC_CFG2,        { "mmsc_cfg2",         any, read_zero, write_stub} },
    {CSR_MVEC_CFG,         { "mvec_cfg",          any, read_zero, write_stub} },

    /* Crash Debug CSRs */
    {CSR_MCRASH_STATESAVE,  { "mcrash_statesave",  any, read_zero, write_stub} },
    {CSR_MSTATUS_CRASHSAVE, { "mstatus_crashsave", any, read_zero, write_stub} },

    /* Memory CSRs */
    {CSR_MILMB,            { "milmb",             any, read_zero, write_stub} },
    {CSR_MDLMB,            { "mdlmb",             any, read_zero, write_stub} },
    {CSR_MECC_CODE,        { "mecc_code",         any, read_zero, write_stub} },
    {CSR_MNVEC,            { "mnvec",             any, read_zero, write_stub} },
    {CSR_MCACHE_CTL,       { "mcache_ctl",        any, read_zero, write_stub} },
    {CSR_MCCTLBEGINADDR,   { "mcctlbeginaddr",    any, read_zero, write_stub} },
    {CSR_MCCTLCOMMAND,     { "mcctlcommand",      any, read_zero, write_stub} },
    {CSR_MCCTLDATA,        { "mcctldata",         any, read_zero, write_stub} },
    {CSR_MPPIB,            { "mppib",             any, read_zero, write_stub} },
    {CSR_MFIOB,            { "mfiob",             any, read_zero, write_stub} },

    /* Hardware Stack Protection & Recording */
    {CSR_MHSP_CTL,         { "mhsp_ctl",          any, read_zero, write_stub} },
    {CSR_MSP_BOUND,        { "msp_bound",         any, read_zero, write_stub} },
    {CSR_MSP_BASE,         { "msp_base",          any, read_zero, write_stub} },
    {CSR_MXSTATUS,         { "mxstatus",          any, read_zero, write_stub} },
    {CSR_MDCAUSE,          { "mdcause",           any, read_zero, write_stub} },
    {CSR_MSLIDELEG,        { "mslideleg",         any, read_zero, write_stub} },
    {CSR_MSAVESTATUS,      { "msavestatus",       any, read_zero, write_stub} },
    {CSR_MSAVEEPC1,        { "msaveepc1",         any, read_zero, write_stub} },
    {CSR_MSAVECAUSE1,      { "msavecause1",       any, read_zero, write_stub} },
    {CSR_MSAVEEPC2,        { "msaveepc2",         any, read_zero, write_stub} },
    {CSR_MSAVECAUSE2,      { "msavecause2",       any, read_zero, write_stub} },
    {CSR_MSAVEDCAUSE1,     { "msavedcause1",      any, read_zero, write_stub} },
    {CSR_MSAVEDCAUSE2,     { "msavedcause2",      any, read_zero, write_stub} },

    /* Control CSRs */
    {CSR_MPFT_CTL,         { "mpft_ctl",          any, read_zero, write_stub} },
    {CSR_MMISC_CTL,        { "mmisc_ctl",         any, read_zero, write_stub} },
    {CSR_MCLK_CTL,         { "mclk_ctl",          any, read_zero, write_stub} },

    /* Counter related CSRs */
    {CSR_MCOUNTERWEN,      { "mcounterwen",       any, read_zero, write_stub} },
    {CSR_MCOUNTERINTEN,    { "mcounterinten",     any, read_zero, write_stub} },
    {CSR_MCOUNTERMASK_M,   { "mcountermask_m",    any, read_zero, write_stub} },
    {CSR_MCOUNTERMASK_S,   { "mcountermask_s",    any, read_zero, write_stub} },
    {CSR_MCOUNTERMASK_U,   { "mcountermask_u",    any, read_zero, write_stub} },
    {CSR_MCOUNTEROVF,      { "mcounterovf",       any, read_zero, write_stub} },

    /* Enhanced CLIC CSRs */
    {CSR_MIRQ_ENTRY,       { "mirq_entry",        any, read_zero, write_stub} },
    {CSR_MINTSEL_JAL,      { "mintsel_jal",       any, read_zero, write_stub} },
    {CSR_PUSHMCAUSE,       { "pushmcause",        any, read_zero, write_stub} },
    {CSR_PUSHMEPC,         { "pushmepc",          any, read_zero, write_stub} },
    {CSR_PUSHMXSTATUS,     { "pushmxstatus",      any, read_zero, write_stub} },

    /* Andes Physical Memory Attribute(PMA) CSRs */
    {CSR_PMACFG0,          { "pmacfg0",           any, read_zero, write_stub} },
    {CSR_PMACFG1,          { "pmacfg1",           any, read_zero, write_stub} },
    {CSR_PMACFG2,          { "pmacfg2",           any, read_zero, write_stub} },
    {CSR_PMACFG3,          { "pmacfg3",           any, read_zero, write_stub} },
    {CSR_PMAADDR0,         { "pmaaddr0",          any, read_zero, write_stub} },
    {CSR_PMAADDR1,         { "pmaaddr1",          any, read_zero, write_stub} },
    {CSR_PMAADDR2,         { "pmaaddr2",          any, read_zero, write_stub} },
    {CSR_PMAADDR3,         { "pmaaddr3",          any, read_zero, write_stub} },
    {CSR_PMAADDR4,         { "pmaaddr4",          any, read_zero, write_stub} },
    {CSR_PMAADDR5,         { "pmaaddr5",          any, read_zero, write_stub} },
    {CSR_PMAADDR6,         { "pmaaddr6",          any, read_zero, write_stub} },
    {CSR_PMAADDR7,         { "pmaaddr7",          any, read_zero, write_stub} },
    {CSR_PMAADDR8,         { "pmaaddr8",          any, read_zero, write_stub} },
    {CSR_PMAADDR9,         { "pmaaddr9",          any, read_zero, write_stub} },
    {CSR_PMAADDR10,        { "pmaaddr10",         any, read_zero, write_stub} },
    {CSR_PMAADDR11,        { "pmaaddr11",         any, read_zero, write_stub} },
    {CSR_PMAADDR12,        { "pmaaddr12",         any, read_zero, write_stub} },
    {CSR_PMAADDR13,        { "pmaaddr13",         any, read_zero, write_stub} },
    {CSR_PMAADDR14,        { "pmaaddr14",         any, read_zero, write_stub} },
    {CSR_PMAADDR15,        { "pmaaddr15",         any, read_zero, write_stub} },

    /* Debug/Trace Registers (shared with Debug Mode) */
    {CSR_TSELECT,          { "tselect",           any, read_zero, write_stub} },
    {CSR_TDATA1,           { "tdata1",            any, read_zero, write_stub} },
    {CSR_TDATA2,           { "tdata2",            any, read_zero, write_stub} },
    {CSR_TDATA3,           { "tdata3",            any, read_zero, write_stub} },
    {CSR_TINFO,            { "tinfo",             any, read_zero, write_stub} },

    /* ========= AndeStar V5 supervisor mode CSRs ========= */
    /* Supervisor trap registers */
    {CSR_SLIE,             { "slie",              any, read_zero, write_stub} },
    {CSR_SLIP,             { "slip",              any, read_zero, write_stub} },
    {CSR_SDCAUSE,          { "sdcause",           any, read_zero, write_stub} },

    /* Supervisor counter registers */
    {CSR_SCOUNTERINTEN,    { "scounterinten",     any, read_zero, write_stub} },
    {CSR_SCOUNTERMASK_M,   { "scountermask_m",    any, read_zero, write_stub} },
    {CSR_SCOUNTERMASK_S,   { "scountermask_s",    any, read_zero, write_stub} },
    {CSR_SCOUNTERMASK_U,   { "scountermask_u",    any, read_zero, write_stub} },
    {CSR_SCOUNTEROVF,      { "scounterovf",       any, read_zero, write_stub} },
    {CSR_SCOUNTINHIBIT,    { "scountinhibit",     any, read_zero, write_stub} },
    {CSR_SHPMEVENT3,       { "shpmevent3",        any, read_zero, write_stub} },
    {CSR_SHPMEVENT4,       { "shpmevent4",        any, read_zero, write_stub} },
    {CSR_SHPMEVENT5,       { "shpmevent5",        any, read_zero, write_stub} },
    {CSR_SHPMEVENT6,       { "shpmevent6",        any, read_zero, write_stub} },

    /* Supervisor control registers */
    {CSR_SCCTLDATA,        { "scctldata",         any, read_zero, write_stub} },
    {CSR_SMISC_CTL,        { "smisc_ctl",         any, read_zero, write_stub} },
    #endif

    /* ========= AndeStar V5 user mode CSRs ========= */
    /* User mode control registers */
    {CSR_UITB,             { "uitb",              any, read_uitb, write_uitb} },
    {CSR_UCODE,            { "ucode",             any, read_zero, write_stub} },
    {CSR_UDCAUSE,          { "udcause",           any, read_zero, write_stub} },
    {CSR_UCCTLBEGINADDR,   { "ucctlbeginaddr",    any, read_zero, write_stub} },
    {CSR_UCCTLCOMMAND,     { "ucctlcommand",      any, read_zero, write_stub} },
    {CSR_WFE,              { "wfe",               any, read_zero, write_stub} },
    {CSR_SLEEPVALUE,       { "sleepvalue",        any, read_zero, write_stub} },
    {CSR_TXEVT,            { "csr_txevt",         any, read_zero, write_stub} },
    {0, { "", NULL, NULL, NULL } },
    };
