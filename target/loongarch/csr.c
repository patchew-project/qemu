/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "csr.h"

#define CSR_OFF_FUNCS(NAME, FL, RD, WR)                    \
    [LOONGARCH_CSR_##NAME] = {                             \
        .name   = (stringify(NAME)),                       \
        .offset = CSR_OFFSET(CSR_##NAME, 0),               \
        .flags = FL, .readfn = RD, .writefn = WR           \
    }

#define CSR_OFF_ARRAY(NAME, N)                                \
    [LOONGARCH_CSR_##NAME(N)] = {                             \
        .name   = (stringify(NAME##N)),                       \
        .offset = CSR_OFFSET(CSR_##NAME[N], 0),               \
        .flags = CSRFL_BASIC, .readfn = NULL, .writefn = NULL \
    }

#define CSR_OFF_FLAGS(NAME, FL)   CSR_OFF_FUNCS(NAME, FL, NULL, NULL)
#define CSR_OFF(NAME)             CSR_OFF_FLAGS(NAME, CSRFL_BASIC)
#define GCSR_OFF_FUNCS(NAME, FL, RD, WR)                  \
    [LOONGARCH_CSR_##NAME] = {                            \
        .name   = (stringify(GCSR_##NAME)),               \
        .offset = CSR_OFFSET(CSR_##NAME, 1),              \
        .flags = FL, .readfn = RD, .writefn = WR          \
    }
#define GCSR_OFF_ARRAY(NAME, N)                               \
    [LOONGARCH_CSR_##NAME(N)] = {                             \
        .name   = (stringify(GCSR_##NAME##N)),                \
        .offset = CSR_OFFSET(CSR_##NAME[N], 1),               \
        .flags = CSRFL_BASIC, .readfn = NULL, .writefn = NULL \
    }
#define GCSR_OFF_FLAGS(NAME, FL) GCSR_OFF_FUNCS(NAME, FL, NULL, NULL)
#define GCSR_OFF(NAME) GCSR_OFF_FLAGS(NAME, CSRFL_BASIC)
#define GCSR_GSPR(NAME) GCSR_OFF_FLAGS(NAME, CSRFL_GSPR)

static CSRInfo csr_info[] = {
    CSR_OFF_FLAGS(CRMD, CSRFL_EXITTB),
    CSR_OFF(PRMD),
    CSR_OFF_FLAGS(EUEN, CSRFL_EXITTB),
    CSR_OFF_FLAGS(MISC, CSRFL_READONLY),
    CSR_OFF(ECFG),
    CSR_OFF_FLAGS(ESTAT, CSRFL_EXITTB),
    CSR_OFF(ERA),
    CSR_OFF(BADV),
    CSR_OFF_FLAGS(BADI, CSRFL_READONLY),
    CSR_OFF(EENTRY),
    CSR_OFF(TLBIDX),
    CSR_OFF(GTLBC),
    CSR_OFF(TRGP),
    CSR_OFF(TLBEHI),
    CSR_OFF(TLBELO0),
    CSR_OFF(TLBELO1),
    CSR_OFF_FLAGS(ASID, CSRFL_EXITTB),
    CSR_OFF(PGDL),
    CSR_OFF(PGDH),
    CSR_OFF_FLAGS(PGD, CSRFL_READONLY),
    CSR_OFF(PWCL),
    CSR_OFF(PWCH),
    CSR_OFF(STLBPS),
    CSR_OFF(RVACFG),
    CSR_OFF_FLAGS(CPUID, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG1, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG2, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG3, CSRFL_READONLY),
    CSR_OFF_ARRAY(SAVE, 0),
    CSR_OFF_ARRAY(SAVE, 1),
    CSR_OFF_ARRAY(SAVE, 2),
    CSR_OFF_ARRAY(SAVE, 3),
    CSR_OFF_ARRAY(SAVE, 4),
    CSR_OFF_ARRAY(SAVE, 5),
    CSR_OFF_ARRAY(SAVE, 6),
    CSR_OFF_ARRAY(SAVE, 7),
    CSR_OFF_ARRAY(SAVE, 8),
    CSR_OFF_ARRAY(SAVE, 9),
    CSR_OFF_ARRAY(SAVE, 10),
    CSR_OFF_ARRAY(SAVE, 11),
    CSR_OFF_ARRAY(SAVE, 12),
    CSR_OFF_ARRAY(SAVE, 13),
    CSR_OFF_ARRAY(SAVE, 14),
    CSR_OFF_ARRAY(SAVE, 15),
    CSR_OFF(TID),
    CSR_OFF_FLAGS(TCFG, CSRFL_IO),
    CSR_OFF_FLAGS(TVAL, CSRFL_READONLY | CSRFL_IO),
    CSR_OFF(CNTC),
    CSR_OFF_FLAGS(TICLR, CSRFL_IO),
    CSR_OFF(GSTAT),
    CSR_OFF(GCFG),
    CSR_OFF_FLAGS(GINTC, CSRFL_IO),
    CSR_OFF(GCNTC),
    CSR_OFF(LLBCTL),
    CSR_OFF(IMPCTL1),
    CSR_OFF(IMPCTL2),
    CSR_OFF(TLBRENTRY),
    CSR_OFF(TLBRBADV),
    CSR_OFF(TLBRERA),
    CSR_OFF(TLBRSAVE),
    CSR_OFF(TLBRELO0),
    CSR_OFF(TLBRELO1),
    CSR_OFF(TLBREHI),
    CSR_OFF(TLBRPRMD),
    CSR_OFF(MERRCTL),
    CSR_OFF(MERRINFO1),
    CSR_OFF(MERRINFO2),
    CSR_OFF(MERRENTRY),
    CSR_OFF(MERRERA),
    CSR_OFF(MERRSAVE),
    CSR_OFF(CTAG),
    CSR_OFF_ARRAY(DMW, 0),
    CSR_OFF_ARRAY(DMW, 1),
    CSR_OFF_ARRAY(DMW, 2),
    CSR_OFF_ARRAY(DMW, 3),
    CSR_OFF_ARRAY(PERFCTRL, 0),
    CSR_OFF_ARRAY(PERFCNTR, 0),
    CSR_OFF_ARRAY(PERFCTRL, 1),
    CSR_OFF_ARRAY(PERFCNTR, 1),
    CSR_OFF_ARRAY(PERFCTRL, 2),
    CSR_OFF_ARRAY(PERFCNTR, 2),
    CSR_OFF_ARRAY(PERFCTRL, 3),
    CSR_OFF_ARRAY(PERFCNTR, 3),
    CSR_OFF_ARRAY(PERFCTRL, 4),
    CSR_OFF_ARRAY(PERFCNTR, 4),
    CSR_OFF_ARRAY(PERFCTRL, 5),
    CSR_OFF_ARRAY(PERFCNTR, 5),
    CSR_OFF_ARRAY(PERFCTRL, 6),
    CSR_OFF_ARRAY(PERFCNTR, 6),
    CSR_OFF_ARRAY(PERFCTRL, 7),
    CSR_OFF_ARRAY(PERFCNTR, 7),
    CSR_OFF_ARRAY(PERFCTRL, 8),
    CSR_OFF_ARRAY(PERFCNTR, 8),
    CSR_OFF_ARRAY(PERFCTRL, 9),
    CSR_OFF_ARRAY(PERFCNTR, 9),
    CSR_OFF_ARRAY(PERFCTRL, 10),
    CSR_OFF_ARRAY(PERFCNTR, 10),
    CSR_OFF_ARRAY(PERFCTRL, 11),
    CSR_OFF_ARRAY(PERFCNTR, 11),
    CSR_OFF_ARRAY(PERFCTRL, 12),
    CSR_OFF_ARRAY(PERFCNTR, 12),
    CSR_OFF_ARRAY(PERFCTRL, 13),
    CSR_OFF_ARRAY(PERFCNTR, 13),
    CSR_OFF_ARRAY(PERFCTRL, 14),
    CSR_OFF_ARRAY(PERFCNTR, 14),
    CSR_OFF_ARRAY(PERFCTRL, 15),
    CSR_OFF_ARRAY(PERFCNTR, 15),
    CSR_OFF(DBG),
    CSR_OFF(DERA),
    CSR_OFF(DSAVE),
    CSR_OFF_ARRAY(MSGIS, 0),
    CSR_OFF_ARRAY(MSGIS, 1),
    CSR_OFF_ARRAY(MSGIS, 2),
    CSR_OFF_ARRAY(MSGIS, 3),
    CSR_OFF(MSGIR),
};

static CSRInfo gcsr_info[] = {
    GCSR_OFF_FLAGS(CRMD, CSRFL_EXITTB),
    GCSR_OFF(PRMD),
    GCSR_OFF_FLAGS(EUEN, CSRFL_EXITTB),
    GCSR_OFF_FLAGS(MISC, CSRFL_GUEST_READONLY),
    GCSR_OFF(ECFG),
    GCSR_OFF_FLAGS(ESTAT, CSRFL_EXITTB),
    GCSR_OFF(ERA),
    GCSR_OFF(BADV),
    GCSR_OFF_FLAGS(BADI, CSRFL_GUEST_READONLY),
    GCSR_OFF(EENTRY),
    GCSR_OFF(TLBIDX),
    GCSR_GSPR(GTLBC),
    GCSR_GSPR(TRGP),
    GCSR_OFF(TLBEHI),
    GCSR_OFF(TLBELO0),
    GCSR_OFF(TLBELO1),
    GCSR_OFF_FLAGS(ASID, CSRFL_EXITTB),
    GCSR_OFF(PGDL),
    GCSR_OFF(PGDH),
    GCSR_OFF_FLAGS(PGD, CSRFL_GUEST_READONLY),
    GCSR_OFF(PWCL),
    GCSR_OFF(PWCH),
    GCSR_OFF(STLBPS),
    GCSR_OFF(RVACFG),
    GCSR_OFF_FLAGS(CPUID, CSRFL_GUEST_READONLY),
    GCSR_OFF_FLAGS(PRCFG1, CSRFL_GUEST_READONLY),
    GCSR_OFF_FLAGS(PRCFG2, CSRFL_GUEST_READONLY),
    GCSR_OFF_FLAGS(PRCFG3, CSRFL_GUEST_READONLY),
    GCSR_OFF_ARRAY(SAVE, 0),
    GCSR_OFF_ARRAY(SAVE, 1),
    GCSR_OFF_ARRAY(SAVE, 2),
    GCSR_OFF_ARRAY(SAVE, 3),
    GCSR_OFF_ARRAY(SAVE, 4),
    GCSR_OFF_ARRAY(SAVE, 5),
    GCSR_OFF_ARRAY(SAVE, 6),
    GCSR_OFF_ARRAY(SAVE, 7),
    GCSR_OFF_ARRAY(SAVE, 8),
    GCSR_OFF_ARRAY(SAVE, 9),
    GCSR_OFF_ARRAY(SAVE, 10),
    GCSR_OFF_ARRAY(SAVE, 11),
    GCSR_OFF_ARRAY(SAVE, 12),
    GCSR_OFF_ARRAY(SAVE, 13),
    GCSR_OFF_ARRAY(SAVE, 14),
    GCSR_OFF_ARRAY(SAVE, 15),
    GCSR_OFF(TID),
    GCSR_OFF_FLAGS(TCFG, CSRFL_IO),
    GCSR_OFF_FLAGS(TVAL, CSRFL_GUEST_READONLY | CSRFL_IO),
    GCSR_OFF(CNTC),
    GCSR_OFF_FLAGS(TICLR, CSRFL_IO),
    GCSR_GSPR(GSTAT),
    GCSR_GSPR(GCFG),
    GCSR_GSPR(GINTC),
    GCSR_GSPR(GCNTC),
    GCSR_OFF(LLBCTL),
    GCSR_GSPR(IMPCTL1),
    GCSR_GSPR(IMPCTL2),
    GCSR_OFF(TLBRENTRY),
    GCSR_OFF(TLBRBADV),
    GCSR_OFF(TLBRERA),
    GCSR_OFF(TLBRSAVE),
    GCSR_OFF(TLBRELO0),
    GCSR_OFF(TLBRELO1),
    GCSR_OFF(TLBREHI),
    GCSR_OFF(TLBRPRMD),
    GCSR_GSPR(MERRCTL),
    GCSR_GSPR(MERRINFO1),
    GCSR_GSPR(MERRINFO2),
    GCSR_GSPR(MERRENTRY),
    GCSR_GSPR(MERRERA),
    GCSR_GSPR(MERRSAVE),
    GCSR_GSPR(CTAG),
    GCSR_OFF_ARRAY(DMW, 0),
    GCSR_OFF_ARRAY(DMW, 1),
    GCSR_OFF_ARRAY(DMW, 2),
    GCSR_OFF_ARRAY(DMW, 3),
    GCSR_GSPR(DBG),
    GCSR_GSPR(DERA),
    GCSR_GSPR(DSAVE),
};

CSRInfo *get_csr(unsigned int csr_num)
{
    CSRInfo *csr;

    if (csr_num >= ARRAY_SIZE(csr_info)) {
        return NULL;
    }

    csr = &csr_info[csr_num];
    if (csr->flags == 0) {
        return NULL;
    }

    return csr;
}

CSRInfo *get_gcsr(unsigned int csr_num)
{
    CSRInfo *csr;

    if (csr_num >= ARRAY_SIZE(gcsr_info)) {
        return NULL;
    }

    csr = &gcsr_info[csr_num];
    if (csr->flags == 0) {
        return NULL;
    }

    return csr;
}

bool set_csr_flag(unsigned int csr_num, int flag)
{
    CSRInfo *csr;

    csr = get_csr(csr_num);
    if (!csr) {
        return false;
    }

    csr->flags |= flag;
    return true;
}
