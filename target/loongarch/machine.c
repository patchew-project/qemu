/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch Machine State
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/cpu.h"
#include "internals.h"

/* FPU state */
static int get_fpr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field)
{
    fpr_t *v = pv;

    qemu_get_sbe64s(f, &v->vreg.D(0));
    return 0;
}

static int put_fpr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    fpr_t *v = pv;

    qemu_put_sbe64s(f, &v->vreg.D(0));
    return 0;
}

static const VMStateInfo vmstate_info_fpr = {
    .name = "fpr",
    .get  = get_fpr,
    .put  = put_fpr,
};

#define VMSTATE_FPR_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_SUB_ARRAY(_f, _s, 0, _n, _v, vmstate_info_fpr, fpr_t)

#define VMSTATE_FPR_ARRAY(_f, _s, _n)                             \
    VMSTATE_FPR_ARRAY_V(_f, _s, _n, 0)

static bool fpu_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, FP);
}

static const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .fields = (VMStateField[]) {
        VMSTATE_FPR_ARRAY(env.fpr, LoongArchCPU, 32),
        VMSTATE_UINT32(env.fcsr0, LoongArchCPU),
        VMSTATE_BOOL_ARRAY(env.cf, LoongArchCPU, 8),
        VMSTATE_END_OF_LIST()
    },
};

/* LSX state*/
static int get_lsx(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field)
{
    int i;
    fpr_t *v = pv;

    for (i = 0; i < LSX_LEN/64; i++) {
        qemu_get_sbe64s(f, &v->vreg.D(i));
    }
    return 0;
}

static int put_lsx(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    int i;
    fpr_t *v = pv;

    for (i = 0; i < LSX_LEN/8; i++) {
        qemu_put_sbe64s(f, &v->vreg.D(i));
    }
    return 0;
}

static const VMStateInfo vmstate_info_lsx = {
    .name = "lsx",
    .get  = get_lsx,
    .put  = put_lsx,
};

#define VMSTATE_LSX_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_SUB_ARRAY(_f, _s, 0, _n, _v, vmstate_info_lsx, fpr_t)

#define VMSTATE_LSX_ARRAY(_f, _s, _n)                             \
    VMSTATE_LSX_ARRAY_V(_f, _s, _n, 0)

static bool lsx_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LSX);
}

static const VMStateDescription vmstate_lsx = {
    .name = "cpu/lsx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lsx_needed,
    .fields = (VMStateField[]) {
        VMSTATE_LSX_ARRAY(env.fpr, LoongArchCPU, 32),
        VMSTATE_END_OF_LIST()
    },
};

/* TLB state */
const VMStateDescription vmstate_tlb = {
    .name = "cpu/tlb",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(tlb_misc, LoongArchTLB),
        VMSTATE_UINT64(tlb_entry0, LoongArchTLB),
        VMSTATE_UINT64(tlb_entry1, LoongArchTLB),
        VMSTATE_END_OF_LIST()
    }
};

/* LoongArch CPU state */
const VMStateDescription vmstate_loongarch_cpu = {
    .name = "cpu",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.gpr, LoongArchCPU, 32),
        VMSTATE_UINTTL(env.pc, LoongArchCPU),

        /* Remaining CSRs */
        VMSTATE_UINT64(env.CSR_CRMD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRMD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_EUEN, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MISC, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ECFG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ESTAT, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_BADV, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_BADI, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_EENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBIDX, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBEHI, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBELO0, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBELO1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ASID, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PGDL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PGDH, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PGD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PWCL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PWCH, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_STLBPS, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_RVACFG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRCFG1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRCFG2, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRCFG3, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.CSR_SAVE, LoongArchCPU, 16),
        VMSTATE_UINT64(env.CSR_TID, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TCFG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TVAL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_CNTC, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TICLR, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_LLBCTL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_IMPCTL1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_IMPCTL2, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRBADV, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRSAVE, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRELO0, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRELO1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBREHI, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRPRMD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRCTL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRINFO1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRINFO2, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRSAVE, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_CTAG, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.CSR_DMW, LoongArchCPU, 4),

        /* Debug CSRs */
        VMSTATE_UINT64(env.CSR_DBG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_DERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_DSAVE, LoongArchCPU),
        /* TLB */
        VMSTATE_STRUCT_ARRAY(env.tlb, LoongArchCPU, LOONGARCH_TLB_MAX,
                             0, vmstate_tlb, LoongArchTLB),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_fpu,
        &vmstate_lsx,
    }
};
