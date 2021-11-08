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

typedef disassemble_info DisasContext;

#define INSNLEN 4

typedef enum {
    la_codec_2r,
    la_codec_2r_u5,
    la_codec_2r_u6,
    la_codec_2r_2bw,
    la_codec_2r_2bd,
    la_codec_3r,
    la_codec_3r_rd0,
    la_codec_3r_sa,
    la_codec_4r,
    la_codec_r_im20,
    la_codec_2r_im16,
    la_codec_2r_im14,
    la_codec_2r_im12,
    la_codec_r_cd,
    la_codec_r_cj,
    la_codec_code,
    la_codec_whint,
    la_codec_r_ofs21,
    la_codec_cj_ofs21,
    la_codec_ofs26,
    la_codec_cond,
    la_codec_sel,

} la_codec;

#define la_fmt_rd_rj           "nt0,1"
#define la_fmt_rj_rk           "nt1,2"
#define la_fmt_rd_si20         "nt0,i(x)"
#define la_fmt_rd_rj_ui        "nt0,1,C"
#define la_fmt_rd_rj_bs        "nt0,1,C,D"
#define la_fmt_rd_rj_si        "nt0,1,i(x)"
#define la_fmt_hint_rj_si12    "ntE,1,i(x)"
#define la_fmt_rd_rj_rk        "nt0,1,2"
#define la_fmt_fd_rj_rk        "nt3,1,2"
#define la_fmt_rd_rj_rk_sa     "nt0,1,2,D"
#define la_fmt_fd_fj           "nt3,4"
#define la_fmt_fd_fj_si12      "nt3,4,i(x)"
#define la_fmt_fcsrd_rj        "ntF,1"
#define la_fmt_rd_fcsrs        "nt0,G"
#define la_fmt_cd_fj           "ntH,4"
#define la_fmt_fd_cj           "nt3,I"
#define la_fmt_fd_fj_fk        "nt3,4,5"
#define la_fmt_code            "ntJ"
#define la_fmt_whint           "ntx"
#define la_fmt_offs26          "nto(X)p"
#define la_fmt_rj_offs21       "nt1,o(X)p"
#define la_fmt_cj_offs21       "ntQ,o(X)p"
#define la_fmt_rd_rj_offs16    "nt0,1,o(X)"
#define la_fmt_rj_rd_offs16    "nt1,0,o(X)p"
#define la_fmt_s_cd_fj_fk      "K.stH,4,5"
#define la_fmt_d_cd_fj_fk      "K.dtH,4,5"
#define la_fmt_fd_fj_fk_fa     "nt3,4,5,6"
#define la_fmt_fd_fj_fk_ca     "nt3,4,5,L"

typedef struct {
    uint32_t pc;
    uint32_t insn;
    int32_t imm;
    int32_t imm2;
    uint16_t code;
    uint8_t codec;
    uint8_t r1;
    uint8_t r2;
    uint8_t r3;
    uint8_t r4;
    uint8_t bit;
} la_decode;

const char * const fccregnames[8] = {
  "$fcc0", "$fcc1", "$fcc2", "$fcc3", "$fcc4", "$fcc5", "$fcc6", "$fcc7",
};

/* operand extractors */
#define IM_12 12
#define IM_14 14
#define IM_15 15
#define IM_16 16
#define IM_20 20
#define IM_21 21
#define IM_26 26

static uint32_t operand_r1(uint32_t insn)
{
    return insn & 0x1f;
}

static uint32_t operand_r2(uint32_t insn)
{
    return (insn >> 5) & 0x1f;
}

static uint32_t operand_r3(uint32_t insn)
{
    return (insn >> 10) & 0x1f;
}

static uint32_t operand_r4(uint32_t insn)
{
    return (insn >> 15) & 0x1f;
}

static uint32_t operand_u6(uint32_t insn)
{
    return (insn >> 10) & 0x3f;
}

static uint32_t operand_bw1(uint32_t insn)
{
    return (insn >> 10) & 0x1f;
}

static uint32_t operand_bw2(uint32_t insn)
{
    return (insn >> 16) & 0x1f;
}

static uint32_t operand_bd1(uint32_t insn)
{
    return (insn >> 10) & 0x3f;
}

static uint32_t operand_bd2(uint32_t insn)
{
    return (insn >> 16) & 0x3f;
}

static uint32_t operand_sa(uint32_t insn)
{
    return (insn >> 15) & 0x3;
}

static int32_t operand_im20(uint32_t insn)
{
    int32_t imm = (int32_t)((insn >> 5) & 0xfffff);
    return imm > (1 << 19) ? imm - (1 << 20) : imm;
}

static int32_t operand_im16(uint32_t insn)
{
    int32_t imm = (int32_t)((insn >> 10) & 0xffff);
    return imm > (1 << 15) ? imm - (1 << 16) : imm;
}

static int32_t operand_im14(uint32_t insn)
{
    int32_t imm = (int32_t)((insn >> 10) & 0x3fff);
    return imm > (1 << 13) ? imm - (1 << 14) : imm;
}

static int32_t operand_im12(uint32_t insn)
{
    int32_t imm = (int32_t)((insn >> 10) & 0xfff);
    return imm > (1 << 11) ? imm - (1 << 12) : imm;
}

static uint32_t operand_cd(uint32_t insn)
{
    return insn & 0x7;
}

static uint32_t operand_cj(uint32_t insn)
{
    return (insn >> 5) & 0x7;
}

static uint32_t operand_code(uint32_t insn)
{
    return insn & 0x7fff;
}

static int32_t operand_whint(uint32_t insn)
{
    int32_t imm = (int32_t)(insn & 0x7fff);
    return imm > (1 << 14) ? imm - (1 << 15) : imm;
}

static int32_t operand_ofs21(uint32_t insn)
{
    int32_t imm = (((int32_t)insn & 0x1f) << 16) |
        ((insn >> 10) & 0xffff);
    return imm > (1 << 20) ? imm - (1 << 21) : imm;
}

static int32_t operand_ofs26(uint32_t insn)
{
    int32_t imm = (((int32_t)insn & 0x3ff) << 16) |
        ((insn >> 10) & 0xffff);
    return imm > (1 << 25) ? imm - (1 << 26) : imm;
}

static uint32_t operand_fcond(uint32_t insn)
{
    return (insn >> 15) & 0x1f;
}

static uint32_t operand_sel(uint32_t insn)
{
    return (insn >> 15) & 0x7;
}

/* decode operands */
static void decode_insn_operands(la_decode *dec)
{
    uint32_t insn = dec->insn;
    switch (dec->codec) {
    case la_codec_2r:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        break;
    case la_codec_2r_u5:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        break;
    case la_codec_2r_u6:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_u6(insn);
        break;
    case la_codec_2r_2bw:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_bw1(insn);
        dec->r4 = operand_bw2(insn);
        break;
    case la_codec_2r_2bd:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_bd1(insn);
        dec->r4 = operand_bd2(insn);
        break;
    case la_codec_3r:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        break;
    case la_codec_3r_rd0:
        dec->r1 = 0;
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        break;
    case la_codec_3r_sa:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        dec->r4 = operand_sa(insn);
        break;
    case la_codec_4r:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        dec->r4 = operand_r4(insn);
        break;
    case la_codec_r_im20:
        dec->r1 = operand_r1(insn);
        dec->imm = operand_im20(insn);
        dec->bit = IM_20;
        break;
    case la_codec_2r_im16:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->imm = operand_im16(insn);
        dec->bit = IM_16;
        break;
    case la_codec_2r_im14:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->imm = operand_im14(insn);
        dec->bit = IM_14;
        break;
    case la_codec_2r_im12:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->imm = operand_im12(insn);
        dec->bit = IM_12;
        break;
    case la_codec_r_cd:
        dec->r1 = operand_cd(insn);
        dec->r2 = operand_r2(insn);
        break;
    case la_codec_r_cj:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_cj(insn);
        break;
    case la_codec_code:
        dec->code = operand_code(insn);
        break;
    case la_codec_whint:
        dec->imm = operand_whint(insn);
        dec->bit = IM_15;
        break;
    case la_codec_r_ofs21:
        dec->imm = operand_ofs21(insn);
        dec->bit = IM_21;
        dec->r2 = operand_r2(insn);
        break;
    case la_codec_cj_ofs21:
        dec->imm = operand_ofs21(insn);
        dec->bit = IM_21;
        dec->r2 = operand_cj(insn);
        break;
    case la_codec_ofs26:
        dec->imm = operand_ofs26(insn);
        dec->bit = IM_26;
        break;
    case la_codec_cond:
        dec->r1 = operand_cd(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        dec->r4 = operand_fcond(insn);
        break;
    case la_codec_sel:
        dec->r1 = operand_r1(insn);
        dec->r2 = operand_r2(insn);
        dec->r3 = operand_r3(insn);
        dec->r4 = operand_sel(insn);
        break;
    }
}

/* format instruction */
static void append(char *s1, const char *s2, size_t n)
{
    size_t l1 = strlen(s1);
    if (n - l1 - 1 > 0) {
        strncat(s1, s2, n - l1);
    }
}

static void format_insn(char *buf, size_t buflen,  const char* name,
                        const char *fmt, la_decode *dec)
{
    char tmp[16];

    while (*fmt) {
        switch (*fmt) {
        case 'n': /* name */
            append(buf, name, buflen);
            break;
        case 's':
            append(buf, "s", buflen);
            break;
        case 'd':
            append(buf, "d", buflen);
            break;
        case 't':
            while (strlen(buf) < 16) {
                append(buf, " ", buflen);
            }
            break;
        case '(':
            append(buf, "(", buflen);
            break;
        case ',':
            append(buf, ",", buflen);
            break;
        case '.':
            append(buf, ".", buflen);
            break;
        case ')':
            append(buf, ")", buflen);
            break;
        case '0': /* rd */
            append(buf, regnames[dec->r1], buflen);
            break;
        case '1': /* rj */
            append(buf, regnames[dec->r2], buflen);
            break;
        case '2': /* rk */
            append(buf, regnames[dec->r3], buflen);
            break;
        case '3': /* fd */
            append(buf, fregnames[dec->r1], buflen);
            break;
        case '4': /* fj */
            append(buf, fregnames[dec->r2], buflen);
            break;
        case '5': /* fk */
            append(buf, fregnames[dec->r3], buflen);
            break;
        case '6': /* fa */
            append(buf, fregnames[dec->r4], buflen);
            break;
        case 'C': /* r3 */
            snprintf(tmp, sizeof(tmp), "%x", dec->r3);
            append(buf, tmp, buflen);
            break;
        case 'D': /* r4 */
            snprintf(tmp, sizeof(tmp), "%x", dec->r4);
            append(buf, tmp, buflen);
            break;
        case 'E': /* r1 */
            snprintf(tmp, sizeof(tmp), "%x", dec->r1);
            append(buf, tmp, buflen);
            break;
        case 'F': /* fcsrd */
            append(buf, regnames[dec->r1], buflen);
            break;
        case 'G': /* fcsrs */
            append(buf, regnames[dec->r2], buflen);
            break;
        case 'H': /* cd */
            append(buf, fccregnames[dec->r1], buflen);
            break;
        case 'I': /* cj */
            append(buf, fccregnames[dec->r2], buflen);
            break;
        case 'J': /* code */
            snprintf(tmp, sizeof(tmp), "0x%x", dec->code);
            append(buf, tmp, buflen);
            break;
        case 'K': /* cond */
            switch (dec->r4) {
            case 0x0:
                append(buf, "caf", buflen);
                break;
            case 0x1:
                append(buf, "saf", buflen);
                break;
            case 0x2:
                append(buf, "clt", buflen);
                break;
            case 0x3:
                append(buf, "slt", buflen);
                break;
            case 0x4:
                append(buf, "ceq", buflen);
                break;
            case 0x5:
                append(buf, "seq", buflen);
                break;
            case 0x6:
                append(buf, "cle", buflen);
                break;
            case 0x7:
                append(buf, "sle", buflen);
                break;
            case 0x8:
                append(buf, "cun", buflen);
                break;
            case 0x9:
                append(buf, "sun", buflen);
                break;
            case 0xA:
                append(buf, "cult", buflen);
                break;
            case 0xB:
                append(buf, "sult", buflen);
                break;
            case 0xC:
                append(buf, "cueq", buflen);
                break;
            case 0xD:
                append(buf, "sueq", buflen);
                break;
            case 0xE:
                append(buf, "cule", buflen);
                break;
            case 0xF:
                append(buf, "sule", buflen);
                break;
            case 0x10:
                append(buf, "cne", buflen);
                break;
            case 0x11:
                append(buf, "sne", buflen);
                break;
            case 0x14:
                append(buf, "cor", buflen);
                break;
            case 0x15:
                append(buf, "sor", buflen);
                break;
            case 0x18:
                append(buf, "cune", buflen);
                break;
            case 0x19:
                append(buf, "sune", buflen);
                break;
            }
            break;
        case 'L': /* ca */
            append(buf, fccregnames[dec->r4], buflen);
            break;
        case 'i': /* sixx d */
            snprintf(tmp, sizeof(tmp), "%d", dec->imm);
            append(buf, tmp, buflen);
            break;
        case 'o': /* offset */
            snprintf(tmp, sizeof(tmp), "%d", (dec->imm) << 2);
            append(buf, tmp, buflen);
            break;
        case 'x': /* sixx x */
            switch (dec->bit) {
            case IM_12:
                snprintf(tmp, sizeof(tmp), "0x%x", (dec->imm) & 0xfff);
                append(buf, tmp, buflen);
                break;
            case IM_14:
                snprintf(tmp, sizeof(tmp), "0x%x", (dec->imm) & 0x3fff);
                append(buf, tmp, buflen);
                break;
            case IM_15:
                snprintf(tmp, sizeof(tmp), "0x%x", (dec->imm) & 0x7fff);
                append(buf, tmp, buflen);
                break;
            case IM_16:
                snprintf(tmp, sizeof(tmp), "0x%x", (dec->imm) & 0xffff);
                append(buf, tmp, buflen);
                break;
            case IM_20:
                snprintf(tmp, sizeof(tmp), "0x%x", (dec->imm) & 0xfffff);
                append(buf, tmp, buflen);
                break;
            default:
                snprintf(tmp, sizeof(tmp), "0x%x", dec->imm);
                append(buf, tmp, buflen);
                break;
            }
            break;
        case 'X': /* offset x*/
            switch (dec->bit) {
            case IM_16:
                snprintf(tmp, sizeof(tmp), "0x%x",
                    ((dec->imm) << 2) & 0xffff);
                append(buf, tmp, buflen);
                break;
            case IM_21:
                snprintf(tmp, sizeof(tmp), "0x%x",
                    ((dec->imm) << 2) & 0x1fffff);
                append(buf, tmp, buflen);
                break;
            case IM_26:
                snprintf(tmp, sizeof(tmp), "0x%x",
                    ((dec->imm) << 2) & 0x3ffffff);
                append(buf, tmp, buflen);
                break;
            default:
                snprintf(tmp, sizeof(tmp), "0x%x", (dec->imm) << 2);
                append(buf, tmp, buflen);
                break;
            }
            break;
        case 'p': /* pc */
            snprintf(tmp, sizeof(tmp), "  # 0x%"PRIx32"",
                dec->pc + ((dec->imm) << 2));
            append(buf, tmp, buflen);
            break;
        default:
            break;
        }
        fmt++;
    }
}

#include "decode-insns.c.inc"

static la_decode dec = {0};

int print_insn_loongarch(bfd_vma memaddr, struct disassemble_info *info)
{
    bfd_byte buffer[INSNLEN];
    unsigned long insn;
    int status;

    status = (*info->read_memory_func)(memaddr, buffer, INSNLEN, info);
    if (status == 0) {
        insn = (uint32_t) bfd_getl32(buffer);
        (*info->fprintf_func)(info->stream, "%08" PRIx64 " ", insn);
    } else {
        (*info->memory_error_func)(status, memaddr, info);
        return -1;
    }

    dec.pc = memaddr;
    dec.insn = insn;
    if (!decode(info, insn)) {
        (*info->fprintf_func)(info->stream, "illegal");
    }

    return INSNLEN;
}

#define INSN(opcode, format, la_codec)                               \
static bool trans_##opcode(disassemble_info *info, arg_##opcode * a) \
{                                                                    \
    char buf[128] = { 0 };                                           \
    dec.codec = la_codec;                                            \
    decode_insn_operands(&dec);                                      \
    format_insn(buf, sizeof(buf), #opcode, format, &dec);            \
    (*info->fprintf_func)(info->stream, "\t%s", buf);                \
    return true;                                                     \
}

INSN(clo_w,        la_fmt_rd_rj,        la_codec_2r)
INSN(clz_w,        la_fmt_rd_rj,        la_codec_2r)
INSN(cto_w,        la_fmt_rd_rj,        la_codec_2r)
INSN(ctz_w,        la_fmt_rd_rj,        la_codec_2r)
INSN(clo_d,        la_fmt_rd_rj,        la_codec_2r)
INSN(clz_d,        la_fmt_rd_rj,        la_codec_2r)
INSN(cto_d,        la_fmt_rd_rj,        la_codec_2r)
INSN(ctz_d,        la_fmt_rd_rj,        la_codec_2r)
INSN(revb_2h,      la_fmt_rd_rj,        la_codec_2r)
INSN(revb_4h,      la_fmt_rd_rj,        la_codec_2r)
INSN(revb_2w,      la_fmt_rd_rj,        la_codec_2r)
INSN(revb_d,       la_fmt_rd_rj,        la_codec_2r)
INSN(revh_2w,      la_fmt_rd_rj,        la_codec_2r)
INSN(revh_d,       la_fmt_rd_rj,        la_codec_2r)
INSN(bitrev_4b,    la_fmt_rd_rj,        la_codec_2r)
INSN(bitrev_8b,    la_fmt_rd_rj,        la_codec_2r)
INSN(bitrev_w,     la_fmt_rd_rj,        la_codec_2r)
INSN(bitrev_d,     la_fmt_rd_rj,        la_codec_2r)
INSN(ext_w_h,      la_fmt_rd_rj,        la_codec_2r)
INSN(ext_w_b,      la_fmt_rd_rj,        la_codec_2r)
INSN(rdtime_d,     la_fmt_rd_rj,        la_codec_2r)
INSN(cpucfg,       la_fmt_rd_rj,        la_codec_2r)
INSN(asrtle_d,     la_fmt_rj_rk,        la_codec_3r_rd0)
INSN(asrtgt_d,     la_fmt_rj_rk,        la_codec_3r_rd0)
INSN(alsl_w,       la_fmt_rd_rj_rk_sa,  la_codec_3r_sa)
INSN(alsl_wu,      la_fmt_rd_rj_rk_sa,  la_codec_3r_sa)
INSN(bytepick_w,   la_fmt_rd_rj_rk_sa,  la_codec_3r_sa)
INSN(bytepick_d,   la_fmt_rd_rj_rk_sa,  la_codec_3r_sa)
INSN(add_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(add_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sub_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sub_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(slt,          la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sltu,         la_fmt_rd_rj_rk,     la_codec_3r)
INSN(maskeqz,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(masknez,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(nor,          la_fmt_rd_rj_rk,     la_codec_3r)
INSN(and,          la_fmt_rd_rj_rk,     la_codec_3r)
INSN(or,           la_fmt_rd_rj_rk,     la_codec_3r)
INSN(xor,          la_fmt_rd_rj_rk,     la_codec_3r)
INSN(orn,          la_fmt_rd_rj_rk,     la_codec_3r)
INSN(andn,         la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sll_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(srl_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sra_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sll_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(srl_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(sra_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(rotr_w,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(rotr_d,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mul_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mulh_w,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mulh_wu,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mul_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mulh_d,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mulh_du,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mulw_d_w,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mulw_d_wu,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(div_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mod_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(div_wu,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mod_wu,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(div_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mod_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(div_du,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(mod_du,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crc_w_b_w,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crc_w_h_w,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crc_w_w_w,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crc_w_d_w,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crcc_w_b_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crcc_w_h_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crcc_w_w_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(crcc_w_d_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(break,        la_fmt_code,         la_codec_code)
INSN(syscall,      la_fmt_code,         la_codec_code)
INSN(alsl_d,       la_fmt_rd_rj_rk_sa,  la_codec_3r_sa)
INSN(slli_w,       la_fmt_rd_rj_ui,     la_codec_2r_u5)
INSN(slli_d,       la_fmt_rd_rj_ui,     la_codec_2r_u6)
INSN(srli_w,       la_fmt_rd_rj_ui,     la_codec_2r_u5)
INSN(srli_d,       la_fmt_rd_rj_ui,     la_codec_2r_u6)
INSN(srai_w,       la_fmt_rd_rj_ui,     la_codec_2r_u5)
INSN(srai_d,       la_fmt_rd_rj_ui,     la_codec_2r_u6)
INSN(rotri_w,      la_fmt_rd_rj_ui,     la_codec_2r_u5)
INSN(rotri_d,      la_fmt_rd_rj_ui,     la_codec_2r_u6)
INSN(bstrins_w,    la_fmt_rd_rj_bs,     la_codec_2r_2bw)
INSN(bstrpick_w,   la_fmt_rd_rj_bs,     la_codec_2r_2bw)
INSN(bstrins_d,    la_fmt_rd_rj_bs,     la_codec_2r_2bd)
INSN(bstrpick_d,   la_fmt_rd_rj_bs,     la_codec_2r_2bd)
INSN(fadd_s,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fadd_d,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fsub_s,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fsub_d,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmul_s,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmul_d,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fdiv_s,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fdiv_d,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmax_s,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmax_d,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmin_s,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmin_d,       la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmaxa_s,      la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmaxa_d,      la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmina_s,      la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fmina_d,      la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fscaleb_s,    la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fscaleb_d,    la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fcopysign_s,  la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fcopysign_d,  la_fmt_fd_fj_fk,     la_codec_3r)
INSN(fabs_s,       la_fmt_fd_fj,        la_codec_2r)
INSN(fabs_d,       la_fmt_fd_fj,        la_codec_2r)
INSN(fneg_s,       la_fmt_fd_fj,        la_codec_2r)
INSN(fneg_d,       la_fmt_fd_fj,        la_codec_2r)
INSN(flogb_s,      la_fmt_fd_fj,        la_codec_2r)
INSN(flogb_d,      la_fmt_fd_fj,        la_codec_2r)
INSN(fclass_s,     la_fmt_fd_fj,        la_codec_2r)
INSN(fclass_d,     la_fmt_fd_fj,        la_codec_2r)
INSN(fsqrt_s,      la_fmt_fd_fj,        la_codec_2r)
INSN(fsqrt_d,      la_fmt_fd_fj,        la_codec_2r)
INSN(frecip_s,     la_fmt_fd_fj,        la_codec_2r)
INSN(frecip_d,     la_fmt_fd_fj,        la_codec_2r)
INSN(frsqrt_s,     la_fmt_fd_fj,        la_codec_2r)
INSN(frsqrt_d,     la_fmt_fd_fj,        la_codec_2r)
INSN(fmov_s,       la_fmt_fd_fj,        la_codec_2r)
INSN(fmov_d,       la_fmt_fd_fj,        la_codec_2r)
INSN(movgr2fr_w,   la_fmt_fd_fj,        la_codec_2r)
INSN(movgr2fr_d,   la_fmt_fd_fj,        la_codec_2r)
INSN(movgr2frh_w,  la_fmt_fd_fj,        la_codec_2r)
INSN(movfr2gr_s,   la_fmt_fd_fj,        la_codec_2r)
INSN(movfr2gr_d,   la_fmt_fd_fj,        la_codec_2r)
INSN(movfrh2gr_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(movgr2fcsr,   la_fmt_fcsrd_rj,     la_codec_2r)
INSN(movfcsr2gr,   la_fmt_rd_fcsrs,     la_codec_2r)
INSN(movfr2cf,     la_fmt_cd_fj,        la_codec_r_cd)
INSN(movcf2fr,     la_fmt_fd_cj,        la_codec_r_cj)
INSN(movgr2cf,     la_fmt_cd_fj,        la_codec_r_cd)
INSN(movcf2gr,     la_fmt_fd_cj,        la_codec_r_cj)
INSN(fcvt_s_d,     la_fmt_fd_fj,        la_codec_2r)
INSN(fcvt_d_s,     la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrm_w_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrm_w_d,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrm_l_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrm_l_d,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrp_w_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrp_w_d,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrp_l_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrp_l_d,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrz_w_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrz_w_d,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrz_l_s,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrz_l_d,  la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrne_w_s, la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrne_w_d, la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrne_l_s, la_fmt_fd_fj,        la_codec_2r)
INSN(ftintrne_l_d, la_fmt_fd_fj,        la_codec_2r)
INSN(ftint_w_s,    la_fmt_fd_fj,        la_codec_2r)
INSN(ftint_w_d,    la_fmt_fd_fj,        la_codec_2r)
INSN(ftint_l_s,    la_fmt_fd_fj,        la_codec_2r)
INSN(ftint_l_d,    la_fmt_fd_fj,        la_codec_2r)
INSN(ffint_s_w,    la_fmt_fd_fj,        la_codec_2r)
INSN(ffint_s_l,    la_fmt_fd_fj,        la_codec_2r)
INSN(ffint_d_w,    la_fmt_fd_fj,        la_codec_2r)
INSN(ffint_d_l,    la_fmt_fd_fj,        la_codec_2r)
INSN(frint_s,      la_fmt_fd_fj,        la_codec_2r)
INSN(frint_d,      la_fmt_fd_fj,        la_codec_2r)
INSN(slti,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(sltui,        la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(addi_w,       la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(addi_d,       la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(lu52i_d,      la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(andi,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ori,          la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(xori,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(rdtimel_w,    la_fmt_rd_rj,        la_codec_2r)
INSN(rdtimeh_w,    la_fmt_rd_rj,        la_codec_2r)
INSN(fmadd_s,      la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fmadd_d,      la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fmsub_s,      la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fmsub_d,      la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fnmadd_s,     la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fnmadd_d,     la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fnmsub_s,     la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fnmsub_d,     la_fmt_fd_fj_fk_fa,  la_codec_4r)
INSN(fcmp_cond_s,  la_fmt_s_cd_fj_fk,   la_codec_cond)
INSN(fcmp_cond_d,  la_fmt_d_cd_fj_fk,   la_codec_cond)
INSN(fsel,         la_fmt_fd_fj_fk_ca,  la_codec_sel)
INSN(addu16i_d,    la_fmt_rd_rj_si,     la_codec_2r_im16)
INSN(lu12i_w,      la_fmt_rd_si20,      la_codec_r_im20)
INSN(lu32i_d,      la_fmt_rd_si20,      la_codec_r_im20)
INSN(pcaddi,       la_fmt_rd_si20,      la_codec_r_im20)
INSN(pcalau12i,    la_fmt_rd_si20,      la_codec_r_im20)
INSN(pcaddu12i,    la_fmt_rd_si20,      la_codec_r_im20)
INSN(pcaddu18i,    la_fmt_rd_si20,      la_codec_r_im20)
INSN(ll_w,         la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(sc_w,         la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(ll_d,         la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(sc_d,         la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(ldptr_w,      la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(stptr_w,      la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(ldptr_d,      la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(stptr_d,      la_fmt_rd_rj_si,     la_codec_2r_im14)
INSN(ld_b,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ld_h,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ld_w,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ld_d,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(st_b,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(st_h,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(st_w,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(st_d,         la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ld_bu,        la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ld_hu,        la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(ld_wu,        la_fmt_rd_rj_si,     la_codec_2r_im12)
INSN(preld,        la_fmt_hint_rj_si12, la_codec_2r_im12)
INSN(fld_s,        la_fmt_fd_fj_si12,   la_codec_2r_im12)
INSN(fst_s,        la_fmt_fd_fj_si12,   la_codec_2r_im12)
INSN(fld_d,        la_fmt_fd_fj_si12,   la_codec_2r_im12)
INSN(fst_d,        la_fmt_fd_fj_si12,   la_codec_2r_im12)
INSN(ldx_b,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ldx_h,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ldx_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ldx_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(stx_b,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(stx_h,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(stx_w,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(stx_d,        la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ldx_bu,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ldx_hu,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ldx_wu,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(fldx_s,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(fldx_d,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(fstx_s,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(fstx_d,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amswap_w,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amswap_d,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amadd_w,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amadd_d,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amand_w,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amand_d,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amor_w,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amor_d,       la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amxor_w,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amxor_d,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_w,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_d,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_w,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_d,      la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_wu,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_du,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_wu,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_du,     la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amswap_db_w,  la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amswap_db_d,  la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amadd_db_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amadd_db_d,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amand_db_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amand_db_d,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amor_db_w,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amor_db_d,    la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amxor_db_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(amxor_db_d,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_db_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_db_d,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_db_w,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_db_d,   la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_db_wu,  la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammax_db_du,  la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_db_wu,  la_fmt_rd_rj_rk,     la_codec_3r)
INSN(ammin_db_du,  la_fmt_rd_rj_rk,     la_codec_3r)
INSN(dbar,         la_fmt_whint,        la_codec_whint)
INSN(ibar,         la_fmt_whint,        la_codec_whint)
INSN(fldgt_s,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fldgt_d,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fldle_s,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fldle_d,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fstgt_s,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fstgt_d,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fstle_s,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(fstle_d,      la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldgt_b,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldgt_h,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldgt_w,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldgt_d,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldle_b,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldle_h,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldle_w,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(ldle_d,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stgt_b,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stgt_h,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stgt_w,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stgt_d,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stle_b,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stle_h,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stle_w,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(stle_d,       la_fmt_fd_rj_rk,     la_codec_3r)
INSN(beqz,         la_fmt_rj_offs21,    la_codec_r_ofs21)
INSN(bnez,         la_fmt_rj_offs21,    la_codec_r_ofs21)
INSN(bceqz,        la_fmt_cj_offs21,    la_codec_cj_ofs21)
INSN(bcnez,        la_fmt_cj_offs21,    la_codec_cj_ofs21)
INSN(jirl,         la_fmt_rd_rj_offs16, la_codec_2r_im16)
INSN(b,            la_fmt_offs26,       la_codec_ofs26)
INSN(bl,           la_fmt_offs26,       la_codec_ofs26)
INSN(beq,          la_fmt_rj_rd_offs16, la_codec_2r_im16)
INSN(bne,          la_fmt_rj_rd_offs16, la_codec_2r_im16)
INSN(blt,          la_fmt_rj_rd_offs16, la_codec_2r_im16)
INSN(bge,          la_fmt_rj_rd_offs16, la_codec_2r_im16)
INSN(bltu,         la_fmt_rj_rd_offs16, la_codec_2r_im16)
INSN(bgeu,         la_fmt_rj_rd_offs16, la_codec_2r_im16)
