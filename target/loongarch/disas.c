/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch Disassembler
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited.
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu.h"

const char * const fccregnames[8] = {
  "$fcc0", "$fcc1", "$fcc2", "$fcc3", "$fcc4", "$fcc5", "$fcc6", "$fcc7",
};

typedef struct {
    disassemble_info *info;
    uint32_t insn;
} DisasContext;

#define output(C, INSN, FMT, ...)                                   \
{                                                                   \
    (C)->info->fprintf_func((C)->info->stream, "%08x   %-9s\t" FMT, \
                            (C)->insn, INSN, ##__VA_ARGS__);        \
}

#include "decode-insns.c.inc"

int print_insn_loongarch(bfd_vma memaddr, struct disassemble_info *info)
{
    bfd_byte buffer[4];
    uint32_t insn;
    int status;

    status = (*info->read_memory_func)(memaddr, buffer, 4, info);
    if (status != 0) {
        (*info->memory_error_func)(status, memaddr, info);
        return -1;
    }
    insn = bfd_getl32(buffer);
    DisasContext ctx = {
        .info = info,
        .insn = insn
    };

    if (!decode(&ctx, insn)) {
        output(&ctx, "illegal", "");
    }
    return 4;
}

static void output_rdrj(DisasContext *ctx, arg_fmt_rdrj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->rd], regnames[a->rj]);
}

static void output_rjrk(DisasContext *ctx, arg_fmt_rjrk *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->rj], regnames[a->rk]);
}

static void output_rdrjrk(DisasContext *ctx, arg_fmt_rdrjrk *a,
                          const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s",
           regnames[a->rd], regnames[a->rj], regnames[a->rk]);
}

static void output_rdrjrksa2(DisasContext *ctx, arg_fmt_rdrjrksa2 *a,
                             const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], regnames[a->rk], a->sa2);
}

static void output_rdrjrksa3(DisasContext *ctx, arg_fmt_rdrjrksa3 *a,
                             const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], regnames[a->rk], a->sa3);
}

static void output_code(DisasContext *ctx, arg_fmt_code *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "0x%x", a->code);
}

static void output_rdrjui5(DisasContext *ctx, arg_fmt_rdrjui5 *a,
                           const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], a->ui5);
}

static void output_rdrjui6(DisasContext *ctx, arg_fmt_rdrjui6 *a,
                           const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], a->ui6);
}

static void output_rdrjmsbwlsbw(DisasContext *ctx,
                                arg_fmt_rdrjmsbwlsbw *a,
                                const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x, 0x%x",
           regnames[a->rd], regnames[a->rj], a->msbw, a->lsbw);
}

static void output_rdrjmsbdlsbd(DisasContext *ctx,
                                arg_fmt_rdrjmsbdlsbd *a,
                                const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x, 0x%x",
           regnames[a->rd], regnames[a->rj], a->msbd, a->lsbd);
}

static void output_fdfjfk(DisasContext *ctx, arg_fmt_fdfjfk *a,
                          const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s",
           fregnames[a->fd], fregnames[a->fj], fregnames[a->fk]);
}

static void output_fdfj(DisasContext *ctx, arg_fmt_fdfj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", fregnames[a->fd], fregnames[a->fj]);
}

static void output_fcsrdrj(DisasContext *ctx, arg_fmt_fcsrdrj *a,
                           const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->fcsrd], regnames[a->rj]);
}

static void output_rdfcsrs(DisasContext *ctx, arg_fmt_rdfcsrs *a,
                           const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->rd], regnames[a->fcsrs]);
}

static void output_cdfj(DisasContext *ctx, arg_fmt_cdfj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", fccregnames[a->cd], fregnames[a->fj]);
}

static void output_cdrj(DisasContext *ctx, arg_fmt_cdrj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", fccregnames[a->cd], regnames[a->rj]);
}

static void output_fdcj(DisasContext *ctx, arg_fmt_fdcj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", fregnames[a->fd], fccregnames[a->cj]);
}

static void output_rdcj(DisasContext *ctx, arg_fmt_rdcj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->rd], fccregnames[a->cj]);
}

static void output_rdrjsi12(DisasContext *ctx, arg_fmt_rdrjsi12 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], (a->si12) & 0xfff);
}

static void output_fdfjfkfa(DisasContext *ctx, arg_fmt_fdfjfkfa *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s, %s",
           fregnames[a->fd], fregnames[a->fj],
           fregnames[a->fk], fregnames[a->fk]);
}

static void output_fdfjfkca(DisasContext *ctx, arg_fmt_fdfjfkca *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s, %s",
           fregnames[a->fd], fregnames[a->fj],
           fregnames[a->fk], fccregnames[a->ca]);
}

static void output_rdrjsi16(DisasContext *ctx, arg_fmt_rdrjsi16 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], (a->si16) & 0xffff);
}

static void output_rdsi20(DisasContext *ctx, arg_fmt_rdsi20 *a,
                          const char *mnemonic)
{
    output(ctx, mnemonic, "%s, 0x%x", regnames[a->rd], (a->si20) & 0xfffff);
}

static void output_rdrjsi14(DisasContext *ctx, arg_fmt_rdrjsi14 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj],  (a->si14) & 0x3fff);
}

static void output_hintrjsi12(DisasContext *ctx, arg_fmt_hintrjsi12 *a,
                              const char *mnemonic)
{
    output(ctx, mnemonic, "0x%x, %s, 0x%x",
           a->hint, regnames[a->rj], (a->si12) & 0xfff);
}

static void output_fdrjsi12(DisasContext *ctx, arg_fmt_fdrjsi12 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           fregnames[a->fd], regnames[a->rj], (a->si12) & 0xfff);
}

static void output_fdrjrk(DisasContext *ctx, arg_fmt_fdrjrk *a,
                          const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s",
           fregnames[a->fd], regnames[a->rj], regnames[a->rk]);
}

static void output_whint(DisasContext *ctx, arg_fmt_whint *a,
                         const char *mnemonic)
{
    output(ctx, mnemonic, "0x%x", a->whint);
}

static void output_rjoffs21(DisasContext *ctx, arg_fmt_rjoffs21 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, 0x%x", regnames[a->rj], (a->offs21) & 0x1fffff);
}

static void output_cjoffs21(DisasContext *ctx, arg_fmt_cjoffs21 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, 0x%x",
           fccregnames[a->cj], (a->offs21) & 0x1fffff);
}

static void output_rdrjoffs16(DisasContext *ctx, arg_fmt_rdrjoffs16 *a,
                              const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], (a->offs16) & 0xffff);
}

static void output_offs(DisasContext *ctx, arg_fmt_offs *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "0x%x", (a->offs) & 0x3ffffff);
}

static void output_rjrdoffs16(DisasContext *ctx, arg_fmt_rjrdoffs16 *a,
                              const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rj], regnames[a->rd], (a->offs16) & 0xffff);
}

static void output_fdrj(DisasContext *ctx, arg_fmt_fdrj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", fregnames[a->fd], regnames[a->rj]);
}

static void output_rdfj(DisasContext *ctx, arg_fmt_rdfj *a,
                        const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->rd], regnames[a->fj]);
}

static void output_rdrjui12(DisasContext *ctx, arg_fmt_rdrjui12 *a,
                            const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x",
           regnames[a->rd], regnames[a->rj], (a->ui12) & 0xfff);
}

#define INSN(insn, type)                                        \
static bool trans_##insn(DisasContext *ctx, arg_fmt_##type * a) \
{                                                               \
    output_##type(ctx, a, #insn);                               \
    return true;                                                \
}

INSN(clo_w,        rdrj)
INSN(clz_w,        rdrj)
INSN(cto_w,        rdrj)
INSN(ctz_w,        rdrj)
INSN(clo_d,        rdrj)
INSN(clz_d,        rdrj)
INSN(cto_d,        rdrj)
INSN(ctz_d,        rdrj)
INSN(revb_2h,      rdrj)
INSN(revb_4h,      rdrj)
INSN(revb_2w,      rdrj)
INSN(revb_d,       rdrj)
INSN(revh_2w,      rdrj)
INSN(revh_d,       rdrj)
INSN(bitrev_4b,    rdrj)
INSN(bitrev_8b,    rdrj)
INSN(bitrev_w,     rdrj)
INSN(bitrev_d,     rdrj)
INSN(ext_w_h,      rdrj)
INSN(ext_w_b,      rdrj)
INSN(rdtime_d,     rdrj)
INSN(cpucfg,       rdrj)
INSN(asrtle_d,     rjrk)
INSN(asrtgt_d,     rjrk)
INSN(alsl_w,       rdrjrksa2)
INSN(alsl_wu,      rdrjrksa2)
INSN(bytepick_w,   rdrjrksa2)
INSN(bytepick_d,   rdrjrksa3)
INSN(add_w,        rdrjrk)
INSN(add_d,        rdrjrk)
INSN(sub_w,        rdrjrk)
INSN(sub_d,        rdrjrk)
INSN(slt,          rdrjrk)
INSN(sltu,         rdrjrk)
INSN(maskeqz,      rdrjrk)
INSN(masknez,      rdrjrk)
INSN(nor,          rdrjrk)
INSN(and,          rdrjrk)
INSN(or,           rdrjrk)
INSN(xor,          rdrjrk)
INSN(orn,          rdrjrk)
INSN(andn,         rdrjrk)
INSN(sll_w,        rdrjrk)
INSN(srl_w,        rdrjrk)
INSN(sra_w,        rdrjrk)
INSN(sll_d,        rdrjrk)
INSN(srl_d,        rdrjrk)
INSN(sra_d,        rdrjrk)
INSN(rotr_w,       rdrjrk)
INSN(rotr_d,       rdrjrk)
INSN(mul_w,        rdrjrk)
INSN(mulh_w,       rdrjrk)
INSN(mulh_wu,      rdrjrk)
INSN(mul_d,        rdrjrk)
INSN(mulh_d,       rdrjrk)
INSN(mulh_du,      rdrjrk)
INSN(mulw_d_w,     rdrjrk)
INSN(mulw_d_wu,    rdrjrk)
INSN(div_w,        rdrjrk)
INSN(mod_w,        rdrjrk)
INSN(div_wu,       rdrjrk)
INSN(mod_wu,       rdrjrk)
INSN(div_d,        rdrjrk)
INSN(mod_d,        rdrjrk)
INSN(div_du,       rdrjrk)
INSN(mod_du,       rdrjrk)
INSN(crc_w_b_w,    rdrjrk)
INSN(crc_w_h_w,    rdrjrk)
INSN(crc_w_w_w,    rdrjrk)
INSN(crc_w_d_w,    rdrjrk)
INSN(crcc_w_b_w,   rdrjrk)
INSN(crcc_w_h_w,   rdrjrk)
INSN(crcc_w_w_w,   rdrjrk)
INSN(crcc_w_d_w,   rdrjrk)
INSN(break,        code)
INSN(syscall,      code)
INSN(alsl_d,       rdrjrksa2)
INSN(slli_w,       rdrjui5)
INSN(slli_d,       rdrjui6)
INSN(srli_w,       rdrjui5)
INSN(srli_d,       rdrjui6)
INSN(srai_w,       rdrjui5)
INSN(srai_d,       rdrjui6)
INSN(rotri_w,      rdrjui5)
INSN(rotri_d,      rdrjui6)
INSN(bstrins_w,    rdrjmsbwlsbw)
INSN(bstrpick_w,   rdrjmsbwlsbw)
INSN(bstrins_d,    rdrjmsbdlsbd)
INSN(bstrpick_d,   rdrjmsbdlsbd)
INSN(fadd_s,       fdfjfk)
INSN(fadd_d,       fdfjfk)
INSN(fsub_s,       fdfjfk)
INSN(fsub_d,       fdfjfk)
INSN(fmul_s,       fdfjfk)
INSN(fmul_d,       fdfjfk)
INSN(fdiv_s,       fdfjfk)
INSN(fdiv_d,       fdfjfk)
INSN(fmax_s,       fdfjfk)
INSN(fmax_d,       fdfjfk)
INSN(fmin_s,       fdfjfk)
INSN(fmin_d,       fdfjfk)
INSN(fmaxa_s,      fdfjfk)
INSN(fmaxa_d,      fdfjfk)
INSN(fmina_s,      fdfjfk)
INSN(fmina_d,      fdfjfk)
INSN(fscaleb_s,    fdfjfk)
INSN(fscaleb_d,    fdfjfk)
INSN(fcopysign_s,  fdfjfk)
INSN(fcopysign_d,  fdfjfk)
INSN(fabs_s,       fdfj)
INSN(fabs_d,       fdfj)
INSN(fneg_s,       fdfj)
INSN(fneg_d,       fdfj)
INSN(flogb_s,      fdfj)
INSN(flogb_d,      fdfj)
INSN(fclass_s,     fdfj)
INSN(fclass_d,     fdfj)
INSN(fsqrt_s,      fdfj)
INSN(fsqrt_d,      fdfj)
INSN(frecip_s,     fdfj)
INSN(frecip_d,     fdfj)
INSN(frsqrt_s,     fdfj)
INSN(frsqrt_d,     fdfj)
INSN(fmov_s,       fdfj)
INSN(fmov_d,       fdfj)
INSN(movgr2fr_w,   fdrj)
INSN(movgr2fr_d,   fdrj)
INSN(movgr2frh_w,  fdrj)
INSN(movfr2gr_s,   rdfj)
INSN(movfr2gr_d,   rdfj)
INSN(movfrh2gr_s,  rdfj)
INSN(movgr2fcsr,   fcsrdrj)
INSN(movfcsr2gr,   rdfcsrs)
INSN(movfr2cf,     cdfj)
INSN(movcf2fr,     fdcj)
INSN(movgr2cf,     cdrj)
INSN(movcf2gr,     rdcj)
INSN(fcvt_s_d,     fdfj)
INSN(fcvt_d_s,     fdfj)
INSN(ftintrm_w_s,  fdfj)
INSN(ftintrm_w_d,  fdfj)
INSN(ftintrm_l_s,  fdfj)
INSN(ftintrm_l_d,  fdfj)
INSN(ftintrp_w_s,  fdfj)
INSN(ftintrp_w_d,  fdfj)
INSN(ftintrp_l_s,  fdfj)
INSN(ftintrp_l_d,  fdfj)
INSN(ftintrz_w_s,  fdfj)
INSN(ftintrz_w_d,  fdfj)
INSN(ftintrz_l_s,  fdfj)
INSN(ftintrz_l_d,  fdfj)
INSN(ftintrne_w_s, fdfj)
INSN(ftintrne_w_d, fdfj)
INSN(ftintrne_l_s, fdfj)
INSN(ftintrne_l_d, fdfj)
INSN(ftint_w_s,    fdfj)
INSN(ftint_w_d,    fdfj)
INSN(ftint_l_s,    fdfj)
INSN(ftint_l_d,    fdfj)
INSN(ffint_s_w,    fdfj)
INSN(ffint_s_l,    fdfj)
INSN(ffint_d_w,    fdfj)
INSN(ffint_d_l,    fdfj)
INSN(frint_s,      fdfj)
INSN(frint_d,      fdfj)
INSN(slti,         rdrjsi12)
INSN(sltui,        rdrjsi12)
INSN(addi_w,       rdrjsi12)
INSN(addi_d,       rdrjsi12)
INSN(lu52i_d,      rdrjsi12)
INSN(andi,         rdrjui12)
INSN(ori,          rdrjui12)
INSN(xori,         rdrjui12)
INSN(rdtimel_w,    rdrj)
INSN(rdtimeh_w,    rdrj)
INSN(fmadd_s,      fdfjfkfa)
INSN(fmadd_d,      fdfjfkfa)
INSN(fmsub_s,      fdfjfkfa)
INSN(fmsub_d,      fdfjfkfa)
INSN(fnmadd_s,     fdfjfkfa)
INSN(fnmadd_d,     fdfjfkfa)
INSN(fnmsub_s,     fdfjfkfa)
INSN(fnmsub_d,     fdfjfkfa)
INSN(fsel,         fdfjfkca)
INSN(addu16i_d,    rdrjsi16)
INSN(lu12i_w,      rdsi20)
INSN(lu32i_d,      rdsi20)
INSN(pcaddi,       rdsi20)
INSN(pcalau12i,    rdsi20)
INSN(pcaddu12i,    rdsi20)
INSN(pcaddu18i,    rdsi20)
INSN(ll_w,         rdrjsi14)
INSN(sc_w,         rdrjsi14)
INSN(ll_d,         rdrjsi14)
INSN(sc_d,         rdrjsi14)
INSN(ldptr_w,      rdrjsi14)
INSN(stptr_w,      rdrjsi14)
INSN(ldptr_d,      rdrjsi14)
INSN(stptr_d,      rdrjsi14)
INSN(ld_b,         rdrjsi12)
INSN(ld_h,         rdrjsi12)
INSN(ld_w,         rdrjsi12)
INSN(ld_d,         rdrjsi12)
INSN(st_b,         rdrjsi12)
INSN(st_h,         rdrjsi12)
INSN(st_w,         rdrjsi12)
INSN(st_d,         rdrjsi12)
INSN(ld_bu,        rdrjsi12)
INSN(ld_hu,        rdrjsi12)
INSN(ld_wu,        rdrjsi12)
INSN(preld,        hintrjsi12)
INSN(fld_s,        fdrjsi12)
INSN(fst_s,        fdrjsi12)
INSN(fld_d,        fdrjsi12)
INSN(fst_d,        fdrjsi12)
INSN(ldx_b,        rdrjrk)
INSN(ldx_h,        rdrjrk)
INSN(ldx_w,        rdrjrk)
INSN(ldx_d,        rdrjrk)
INSN(stx_b,        rdrjrk)
INSN(stx_h,        rdrjrk)
INSN(stx_w,        rdrjrk)
INSN(stx_d,        rdrjrk)
INSN(ldx_bu,       rdrjrk)
INSN(ldx_hu,       rdrjrk)
INSN(ldx_wu,       rdrjrk)
INSN(fldx_s,       fdrjrk)
INSN(fldx_d,       fdrjrk)
INSN(fstx_s,       fdrjrk)
INSN(fstx_d,       fdrjrk)
INSN(amswap_w,     rdrjrk)
INSN(amswap_d,     rdrjrk)
INSN(amadd_w,      rdrjrk)
INSN(amadd_d,      rdrjrk)
INSN(amand_w,      rdrjrk)
INSN(amand_d,      rdrjrk)
INSN(amor_w,       rdrjrk)
INSN(amor_d,       rdrjrk)
INSN(amxor_w,      rdrjrk)
INSN(amxor_d,      rdrjrk)
INSN(ammax_w,      rdrjrk)
INSN(ammax_d,      rdrjrk)
INSN(ammin_w,      rdrjrk)
INSN(ammin_d,      rdrjrk)
INSN(ammax_wu,     rdrjrk)
INSN(ammax_du,     rdrjrk)
INSN(ammin_wu,     rdrjrk)
INSN(ammin_du,     rdrjrk)
INSN(amswap_db_w,  rdrjrk)
INSN(amswap_db_d,  rdrjrk)
INSN(amadd_db_w,   rdrjrk)
INSN(amadd_db_d,   rdrjrk)
INSN(amand_db_w,   rdrjrk)
INSN(amand_db_d,   rdrjrk)
INSN(amor_db_w,    rdrjrk)
INSN(amor_db_d,    rdrjrk)
INSN(amxor_db_w,   rdrjrk)
INSN(amxor_db_d,   rdrjrk)
INSN(ammax_db_w,   rdrjrk)
INSN(ammax_db_d,   rdrjrk)
INSN(ammin_db_w,   rdrjrk)
INSN(ammin_db_d,   rdrjrk)
INSN(ammax_db_wu,  rdrjrk)
INSN(ammax_db_du,  rdrjrk)
INSN(ammin_db_wu,  rdrjrk)
INSN(ammin_db_du,  rdrjrk)
INSN(dbar,         whint)
INSN(ibar,         whint)
INSN(fldgt_s,      fdrjrk)
INSN(fldgt_d,      fdrjrk)
INSN(fldle_s,      fdrjrk)
INSN(fldle_d,      fdrjrk)
INSN(fstgt_s,      fdrjrk)
INSN(fstgt_d,      fdrjrk)
INSN(fstle_s,      fdrjrk)
INSN(fstle_d,      fdrjrk)
INSN(ldgt_b,       rdrjrk)
INSN(ldgt_h,       rdrjrk)
INSN(ldgt_w,       rdrjrk)
INSN(ldgt_d,       rdrjrk)
INSN(ldle_b,       rdrjrk)
INSN(ldle_h,       rdrjrk)
INSN(ldle_w,       rdrjrk)
INSN(ldle_d,       rdrjrk)
INSN(stgt_b,       rdrjrk)
INSN(stgt_h,       rdrjrk)
INSN(stgt_w,       rdrjrk)
INSN(stgt_d,       rdrjrk)
INSN(stle_b,       rdrjrk)
INSN(stle_h,       rdrjrk)
INSN(stle_w,       rdrjrk)
INSN(stle_d,       rdrjrk)
INSN(beqz,         rjoffs21)
INSN(bnez,         rjoffs21)
INSN(bceqz,        cjoffs21)
INSN(bcnez,        cjoffs21)
INSN(jirl,         rdrjoffs16)
INSN(b,            offs)
INSN(bl,           offs)
INSN(beq,          rjrdoffs16)
INSN(bne,          rjrdoffs16)
INSN(blt,          rjrdoffs16)
INSN(bge,          rjrdoffs16)
INSN(bltu,         rjrdoffs16)
INSN(bgeu,         rjrdoffs16)

#define output_fcmp(C, PREFIX, SUBFFIX)                                     \
{                                                                           \
    (C)->info->fprintf_func((C)->info->stream, "%08x   %s%s\t%s, %s, %s",   \
                            (C)->insn, PREFIX, SUBFFIX, fccregnames[a->cd], \
                            fregnames[a->fj], fregnames[a->fk]);            \
}

static void output_cdfjfkfcond(DisasContext *ctx, arg_fmt_cdfjfkfcond * a,
                               const char *suffix)
{
    switch (a->fcond) {
    case 0x0:
        output_fcmp(ctx, "fcmp_caf_", suffix);
        break;
    case 0x1:
        output_fcmp(ctx, "fcmp_saf_", suffix);
        break;
    case 0x2:
        output_fcmp(ctx, "fcmp_clt_", suffix);
        break;
    case 0x3:
        output_fcmp(ctx, "fcmp_slt_", suffix);
        break;
    case 0x4:
        output_fcmp(ctx, "fcmp_ceq_", suffix);
        break;
    case 0x5:
        output_fcmp(ctx, "fcmp_seq_", suffix);
        break;
    case 0x6:
        output_fcmp(ctx, "fcmp_cle_", suffix);
        break;
    case 0x7:
        output_fcmp(ctx, "fcmp_sle_", suffix);
        break;
    case 0x8:
        output_fcmp(ctx, "fcmp_cun_", suffix);
        break;
    case 0x9:
        output_fcmp(ctx, "fcmp_sun_", suffix);
        break;
    case 0xA:
        output_fcmp(ctx, "fcmp_cult_", suffix);
        break;
    case 0xB:
        output_fcmp(ctx, "fcmp_sult_", suffix);
        break;
    case 0xC:
        output_fcmp(ctx, "fcmp_cueq_", suffix);
        break;
    case 0xD:
        output_fcmp(ctx, "fcmp_sueq_", suffix);
        break;
    case 0xE:
        output_fcmp(ctx, "fcmp_cule_", suffix);
        break;
    case 0xF:
        output_fcmp(ctx, "fcmp_sule_", suffix);
        break;
    case 0x10:
        output_fcmp(ctx, "fcmp_cne_", suffix);
        break;
    case 0x11:
        output_fcmp(ctx, "fcmp_sne_", suffix);
        break;
    case 0x14:
        output_fcmp(ctx, "fcmp_cor_", suffix);
        break;
    case 0x15:
        output_fcmp(ctx, "fcmp_sor_", suffix);
        break;
    case 0x18:
        output_fcmp(ctx, "fcmp_cune_", suffix);
        break;
    case 0x19:
        output_fcmp(ctx, "fcmp_sune_", suffix);
        break;
    default:
        break;
    }
}

#define FCMP_INSN(insn, suffix, type)                           \
static bool trans_##insn(DisasContext *ctx, arg_fmt_##type * a) \
{                                                               \
    output_##type(ctx, a, #suffix);                             \
    return true;                                                \
}

FCMP_INSN(fcmp_cond_s, s, cdfjfkfcond)
FCMP_INSN(fcmp_cond_d, d, cdfjfkfcond)
