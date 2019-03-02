/*
 * Renesas RX Disassembler
 *
 * Copyright (c) 2019 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "disas/bfd.h"
#include "qemu/bitops.h"
#include "cpu.h"

typedef struct DisasContext {
    disassemble_info *dis;
    uint32_t addr;
} DisasContext;


static uint32_t decode_load_bytes(DisasContext *ctx, uint32_t insn,
                           int i, int n)
{
    bfd_byte buf;
    while (++i <= n) {
        ctx->dis->read_memory_func(ctx->addr++, &buf, 1, ctx->dis);
        insn |= buf << (32 - i * 8);
    }
    return insn;
}

static int32_t li(DisasContext *ctx, int sz)
{
    int32_t addr;
    bfd_byte buf[4];
    addr = ctx->addr;

    switch (sz) {
    case 1:
        ctx->addr += 1;
        ctx->dis->read_memory_func(addr, buf, 1, ctx->dis);
        return buf[0];
    case 2:
        ctx->addr += 2;
        ctx->dis->read_memory_func(addr, buf, 2, ctx->dis);
        return buf[1] << 8 | buf[0];
    case 3:
        ctx->addr += 3;
        ctx->dis->read_memory_func(addr, buf, 3, ctx->dis);
        return buf[2] << 16 | buf[1] << 8 | buf[0];
    case 0:
        ctx->addr += 4;
        ctx->dis->read_memory_func(addr, buf, 4, ctx->dis);
        return buf[3] << 24 | buf[2] << 16 | buf[1] << 8 | buf[0];
    default:
        g_assert_not_reached();
    }
}

/* Include the auto-generated decoder.  */
#include "decode.inc.c"

#define prt(...) (ctx->dis->fprintf_func)((ctx->dis->stream), __VA_ARGS__)

#define RX_MEMORY_BYTE 0
#define RX_MEMORY_WORD 1
#define RX_MEMORY_LONG 2

#define RX_MI_BYTE 0
#define RX_MI_WORD 1
#define RX_MI_LONG 2
#define RX_MI_UWORD 3

static const char size[] = {'b', 'w', 'l'};
static const char *cond[] = {
    "eq", "ne", "c", "nc", "gtu", "leu", "pz", "n",
    "ge", "lt", "gt", "le", "o", "no", "ra", "f"
};
static const char *cr[] = {
    "psw", "", "usp", "fpsw", "", "", "", "",
    "bpsw", "bpc", "isp", "fintv", "intb", "", "", "",
};
static const char *msize[] = {
    "b", "w", "l", "ub", "uw",
};

static const char psw[] = {
    'c', 'z', 's', 'o', 0, 0, 0, 0,
    'i', 'u', 0, 0, 0, 0, 0, 0,
};

static uint32_t rx_index_addr(int ld, int size, DisasContext *ctx)
{
    bfd_byte buf[2];
    switch (ld) {
    case 0:
        return 0;
    case 1:
        ctx->dis->read_memory_func(ctx->addr, buf, 1, ctx->dis);
        ctx->addr += 1;
        return buf[0];
    case 2:
        ctx->dis->read_memory_func(ctx->addr, buf, 2, ctx->dis);
        ctx->addr += 2;
        return buf[1] << 8 | buf[0];
    }
    g_assert_not_reached();
}

static void operand(DisasContext *ctx, int ld, int mi, int rs, int rd)
{
    int dsp;
    const char *mis;
    static const char *sizes[] = {".b", ".w", ".l"};
    if (ld < 3) {
        switch (mi) {
        case 4:
            /* dsp[rs].ub */
            dsp = rx_index_addr(ld, RX_MEMORY_BYTE, ctx);
            mis = ".ub";
            break;
        case 3:
            /* dsp[rs].uw */
            dsp = rx_index_addr(ld, RX_MEMORY_BYTE, ctx);
            mis = ".uw";
            break;
        default:
            dsp = rx_index_addr(ld, mi, ctx);
            mis = sizes[mi];
            break;
        }
        if (dsp > 0) {
            dsp <<= ld;
            prt("%d", dsp);
        }
        prt("[r%d]%s", rs, mis);
    } else {
        prt("r%d", rs);
    }
    prt(",r%d", rd);
}

/* mov.[bwl] rs,dsp:[rd] */
static bool trans_MOV_mr(DisasContext *ctx, arg_MOV_mr *a)
{
    if (a->dsp > 0) {
        prt("mov.%c\tr%d,%d[r%d]",
            size[a->sz], a->rs, a->dsp << a->sz, a->rd);
    } else {
        prt("mov.%c\tr%d,[r%d]",
            size[a->sz], a->rs, a->rd);
    }
    return true;
}

/* mov.[bwl] dsp:[rd],rs */
static bool trans_MOV_rm(DisasContext *ctx, arg_MOV_rm *a)
{
    if (a->dsp > 0) {
        prt("mov.%c\t%d[r%d],r%d",
            size[a->sz], a->dsp << a->sz, a->rd, a->rs);
    } else {
        prt("mov.%c\t[r%d],r%d",
            size[a->sz], a->rd, a->rs);
    }
    return true;
}

/* mov.l #uimm4,rd */
/* mov.l #uimm8,rd */
static bool trans_MOV_ri(DisasContext *ctx, arg_MOV_ri *a)
{
    prt("mov.l\t#%d,r%d", a->imm & 0xff, a->rd);
    return true;
}

/* mov.[bwl] #uimm8,dsp:[rd] */
static bool trans_MOV_mi(DisasContext *ctx, arg_MOV_mi *a)
{
    if (a->dsp > 0) {
        prt("mov.%c\t#%d,%d[r%d]",
            size[a->sz], a->imm & 0xff, a->dsp << a->sz, a->rd);
    } else {
        prt("mov.%c\t#%d,[r%d]",
            size[a->sz], a->imm & 0xff, a->rd);
    }
    return true;
}

/* mov.l #imm,rd */
static bool trans_MOV_rli(DisasContext *ctx, arg_MOV_rli *a)
{
    prt("mov.l\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}


/* mov #imm, dsp:[rd] */
static bool trans_MOV_mli(DisasContext *ctx, arg_MOV_mli *a)
{
    if (a->ld == 2) {
        a->dsp = bswap_16(a->dsp);
    }
    if (a->dsp > 0) {
        prt("mov.%c\t#0x%08x,%d[r%d]",
            size[a->sz], a->imm, a->dsp << a->sz, a->rd);
    } else {
        prt("mov.%c\t#0x%08x,[r%d]",
            size[a->sz], a->imm, a->rd);
    }
    return true;
}


/* mov.[bwl] [ri,rb],rd */
static bool trans_MOV_ra(DisasContext *ctx, arg_MOV_ra *a)
{
    prt("mov.%c\t[r%d,r%d],r%d", size[a->sz], a->ri, a->rb, a->rd);
    return true;
}

/* mov.[bwl] rd,[ri,rb] */
static bool trans_MOV_ar(DisasContext *ctx, arg_MOV_ar *a)
{
    prt("mov.%c\tr%d,[r%d,r%d]", size[a->sz], a->rs, a->ri, a->rb);
    return true;
}


/* mov.[bwl] dsp:[rs],dsp:[rd] */
/* mov.[bwl] rs,dsp:[rd] */
/* mov.[bwl] dsp:[rs],rd */
/* mov.[bwl] rs,rd */
static bool trans_MOV_ll(DisasContext *ctx, arg_MOV_ll *a)
{
    int rs, rd, dsp;

    if (a->lds == 3 && a->ldd < 3) {
        rs = a->rd;
        rd = a->rs;
    } else {
        rs = a->rs;
        rd = a->rd;
    }
    prt("mov.%c\t", size[a->sz]);
    if (a->lds < 3) {
        dsp = rx_index_addr(a->lds, a->sz, ctx);
        if (dsp > 0) {
            prt("%d", dsp);
        }
        prt("[r%d],", rs);
    } else {
        prt("r%d,", rs);
    }
    if (a->ldd < 3) {
        dsp = rx_index_addr(a->ldd, a->sz, ctx);
        if (dsp > 0) {
            prt("%d", dsp);
        }
        prt("[r%d]", rd);
    } else {
        prt("r%d", rd);
    }
    return true;
}

/* mov.[bwl] rs,[rd+] */
/* mov.[bwl] rs,[-rd] */
static bool trans_MOV_pr(DisasContext *ctx, arg_MOV_pr *a)
{
    prt("mov.%c\tr%d,", size[a->sz], a->rs);
    if (a->ad == 0) {
        prt("[r%d+]", a->rd);
    } else {
        prt("[-r%d]", a->rd);
    }
    return true;
}

/* mov.[bwl] [rd+],rs */
/* mov.[bwl] [-rd],rs */
static bool trans_MOV_rp(DisasContext *ctx, arg_MOV_rp *a)
{
    prt("mov.%c\t", size[a->sz]);
    if (a->ad == 1) {
        prt("[-r%d]", a->rd);
    } else {
        prt("[r%d+]", a->rd);
    }
    prt(",r%d", a->rs);
    return true;
}

/* movu.[bw] dsp5:[rs],rd */
static bool trans_MOVU_rm(DisasContext *ctx, arg_MOVU_rm *a)
{
    if (a->dsp > 0) {
        prt("movu.%c%d[r%d],r%d", size[a->sz], a->dsp << a->sz, a->rs, a->rd);
    } else {
        prt("movu.%c[r%d],r%d", size[a->sz], a->rs, a->rd);
    }
    return true;
}

/* movu.[bw] rs,rd */
/* movu.[bw] dsp:[rs],rd */
static bool trans_MOVU_rl(DisasContext *ctx, arg_MOVU_rl *a)
{
    int dsp;
    prt("movu.%c\t", size[a->sz]);
    if (a->ld < 3) {
        /* from memory */
        dsp = rx_index_addr(a->ld, a->sz, ctx);
        if (dsp > 0) {
            prt("%d", dsp);
        }
        prt("[r%d]", a->rs);
    } else {
        prt("r%d", a->rs);
    }
    prt(",r%d", a->rd);
    return true;
}

/* movu.[bw] [ri,rb],rd */
static bool trans_MOVU_ra(DisasContext *ctx, arg_MOVU_ra *a)
{
    prt("mov.%c\t[r%d,r%d],r%d", size[a->sz], a->ri, a->rb, a->rd);
    return true;
}

/* movu.[bw] [rs+],rd */
/* movu.[bw] [-rs],rd */
static bool trans_MOVU_rp(DisasContext *ctx, arg_MOVU_rp *a)
{
    prt("movu.%c\t", size[a->sz]);
    if (a->ad == 1) {
        prt("[-r%d]", a->rd);
    } else {
        prt("[r%d+]", a->rd);
    }
    prt(",r%d", a->rs);
    return true;
}

/* pop rd */
static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    prt("pop\tr%d", a->rd);
    return true;
}

/* popc rx */
static bool trans_POPC(DisasContext *ctx, arg_POPC *a)
{
    prt("pop\tr%s", cr[a->cr]);
    return true;
}

/* popm rd-rd2 */
static bool trans_POPM(DisasContext *ctx, arg_POPM *a)
{
    prt("popm\tr%d-r%d", a->rd, a->rd2);
    return true;
}

/* push rs */
static bool trans_PUSH_r(DisasContext *ctx, arg_PUSH_r *a)
{
    prt("push\tr%d", a->rs);
    return true;
}

/* push dsp[rs] */
static bool trans_PUSH_m(DisasContext *ctx, arg_PUSH_m *a)
{
    prt("push\t");
    int dsp = rx_index_addr(a->ld, a->sz, ctx);
    if (dsp > 0) {
        prt("%d", dsp);
    }
    prt("[r%d]", a->rs);
    return true;
}

/* pushc rx */
static bool trans_PUSHC(DisasContext *ctx, arg_PUSHC *a)
{
    prt("push\t%s", cr[a->cr]);
    return true;
}

/* pushm rs-rs2*/
static bool trans_PUSHM(DisasContext *ctx, arg_PUSHM *a)
{
    prt("pushm\tr%d-r%d", a->rs, a->rs2);
    return true;
}

/* xchg rs,rd */
/* xchg dsp[rs].<mi>,rd */
static bool trans_XCHG_rl(DisasContext *ctx, arg_XCHG_rl *a)
{
    int dsp;

    prt("xchg\t");
    if (a->ld == 3) {
        /* xchg rs,rd */
        prt("r%d", a->rs);
    } else {
        dsp = rx_index_addr(a->ld, a->mi, ctx);
        if (dsp > 0) {
            prt("%d", dsp);
        }
        prt("[r%d].%s", a->rs, msize[a->mi]);
    }
    prt(",r%d", a->rd);
    return true;
}

/* stz #imm,rd */
static bool trans_STZ(DisasContext *ctx, arg_STZ *a)
{
    prt("stz\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* stnz #imm,rd */
static bool trans_STNZ(DisasContext *ctx, arg_STNZ *a)
{
    prt("stnz\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* rtsd #imm */
static bool trans_RTSD_i(DisasContext *ctx, arg_RTSD_i *a)
{
    prt("rtsd\t#%d", (a->imm & 0xff) << 2);
    return true;
}

/* rtsd #imm, rd-rd2 */
static bool trans_RTSD_irr(DisasContext *ctx, arg_RTSD_irr *a)
{
    prt("rtsd\t#%d,r%d-r%d", (a->imm & 0xff) << 2, a->rd, a->rd2);
    return true;
}

/* and #uimm:4, rd */
static bool trans_AND_ri(DisasContext *ctx, arg_AND_ri *a)
{
    prt("and\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* and #imm, rd */
static bool trans_AND_rli(DisasContext *ctx, arg_AND_rli *a)
{
    prt("and\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* and dsp[rs], rd */
/* and rs,rd */
static bool trans_AND_rl(DisasContext *ctx, arg_AND_rl *a)
{
    prt("and\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* and rs,rs2,rd */
static bool trans_AND_rrr(DisasContext *ctx, arg_AND_rrr *a)
{
    prt("and\tr%d,r%d,r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* or #uimm:4, rd */
static bool trans_OR_ri(DisasContext *ctx, arg_OR_ri *a)
{
    prt("or\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* or #imm, rd */
static bool trans_OR_rli(DisasContext *ctx, arg_OR_rli *a)
{
    prt("or\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* or dsp[rs], rd */
/* or rs,rd */
static bool trans_OR_rl(DisasContext *ctx, arg_OR_rl *a)
{
    prt("or\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* or rs,rs2,rd */
static bool trans_OR_rrr(DisasContext *ctx, arg_OR_rrr *a)
{
    prt("or\tr%d,r%d,r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* xor #imm, rd */
static bool trans_XOR_rli(DisasContext *ctx, arg_XOR_rli *a)
{
    prt("xor\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* xor dsp[rs], rd */
/* xor rs,rd */
static bool trans_XOR_rl(DisasContext *ctx, arg_XOR_rl *a)
{
    prt("xor\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* tst #imm, rd */
static bool trans_TST_rli(DisasContext *ctx, arg_TST_rli *a)
{
    prt("tst\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* tst dsp[rs], rd */
/* tst rs, rd */
static bool trans_TST_rl(DisasContext *ctx, arg_TST_rl *a)
{
    prt("tst\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* not rd */
/* not rs, rd */
static bool trans_NOT_rr(DisasContext *ctx, arg_NOT_rr *a)
{
    prt("not\t");
    if (a->rs < 16) {
        prt("r%d", a->rs);
    }
    prt("r%d", a->rd);
    return true;
}

/* neg rd */
/* neg rs, rd */
static bool trans_NEG_rr(DisasContext *ctx, arg_NEG_rr *a)
{
    prt("neg\t");
    if (a->rs < 16) {
        prt("r%d", a->rs);
    }
    prt("r%d", a->rd);
    return true;
}

/* adc #imm, rd */
static bool trans_ADC_rli(DisasContext *ctx, arg_ADC_rli *a)
{
    prt("adc\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* adc rs, rd */
static bool trans_ADC_rr(DisasContext *ctx, arg_ADC_rr *a)
{
    prt("adc\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* adc dsp[rs], rd */
static bool trans_ADC_rl(DisasContext *ctx, arg_ADC_rl *a)
{
    int dsp;
    prt("adc\t");
    dsp = rx_index_addr(a->ld, RX_LONG, ctx);
    if (dsp > 0) {
        prt("%d", dsp);
    }
    prt("[r%d],r%d", a->rs, a->rd);
    return true;
}

/* add #uimm4, rd */
static bool trans_ADD_rri(DisasContext *ctx, arg_ADD_rri *a)
{
    prt("add\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* add rs, rd */
/* add dsp[rs], rd */
static bool trans_ADD_rl(DisasContext *ctx, arg_ADD_rl *a)
{
    prt("add\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* add #imm, rs, rd */
static bool trans_ADD_rrli(DisasContext *ctx, arg_ADD_rrli *a)
{
    prt("add\t#0x%08x,r%d,r%d", a->imm, a->rs2, a->rd);
    return true;
}

/* add rs, rs2, rd */
static bool trans_ADD_rrr(DisasContext *ctx, arg_ADD_rrr *a)
{
    prt("add\tr%d,r%d,r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* cmp #imm4, rd */
/* cmp #imm8, rd */
static bool trans_CMP_ri(DisasContext *ctx, arg_CMP_ri *a)
{
    int rs;
    rs = (a->rs2 < 16) ? a->rs2 : a->rd;
    prt("cmp\t#%d,r%d", a->imm & 0xff, rs);
    return true;
}

/* cmp #imm, rs2 */
static bool trans_CMP_rli(DisasContext *ctx, arg_CMP_rli *a)
{
    prt("cmp\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* cmp rs, rs2 */
/* cmp dsp[rs], rs2 */
static bool trans_CMP_rl(DisasContext *ctx, arg_CMP_rl *a)
{
    prt("cmp\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* sub #imm4, rd */
static bool trans_SUB_ri(DisasContext *ctx, arg_SUB_ri *a)
{
    prt("sub\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* sub rs, rd */
/* sub dsp[rs], rd */
static bool trans_SUB_rl(DisasContext *ctx, arg_SUB_rl *a)
{
    prt("sub\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* sub rs, rs2, rd */
static bool trans_SUB_rrr(DisasContext *ctx, arg_SUB_rrr *a)
{
    prt("sub\tr%d,r%d,r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* sbb rs, rd */
static bool trans_SBB_rr(DisasContext *ctx, arg_SBB_rr *a)
{
    prt("sbb\tr%d,%d", a->rs, a->rd);
    return true;
}

/* sbb dsp[rs], rd */
static bool trans_SBB_rl(DisasContext *ctx, arg_SBB_rl *a)
{
    prt("sbb\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* abs rd */
/* abs rs, rd */
static bool trans_ABS_rr(DisasContext *ctx, arg_ABS_rr *a)
{
    prt("abs\t");
    if (a->rs < 16) {
        prt("r%d,r%d", a->rs, a->rd);
    } else {
        prt("r%d", a->rd);
    }
    return true;
}

/* max #imm, rd */
static bool trans_MAX_ri(DisasContext *ctx, arg_MAX_ri *a)
{
    prt("max\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* max rs, rd */
/* max dsp[rs], rd */
static bool trans_MAX_rl(DisasContext *ctx, arg_MAX_rl *a)
{
    prt("max\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* min #imm, rd */
static bool trans_MIN_ri(DisasContext *ctx, arg_MIN_ri *a)
{
    prt("min\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* min rs, rd */
/* min dsp[rs], rd */
static bool trans_MIN_rl(DisasContext *ctx, arg_MIN_rl *a)
{
    prt("max\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* mul #uimm4, rd */
static bool trans_MUL_ri(DisasContext *ctx, arg_MUL_ri *a)
{
    prt("mul\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* mul #imm, rd */
static bool trans_MUL_rli(DisasContext *ctx, arg_MUL_rli *a)
{
    prt("mul\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* mul rs, rd */
/* mul dsp[rs], rd */
static bool trans_MUL_rl(DisasContext *ctx, arg_MUL_rl *a)
{
    prt("mul\t");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* mul rs, rs2, rd */
static bool trans_MUL_rrr(DisasContext *ctx, arg_MUL_rrr *a)
{
    prt("mul\tr%d,r%d,r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* emul #imm, rd */
static bool trans_EMUL_ri(DisasContext *ctx, arg_EMUL_ri *a)
{
    prt("emul\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* emul rs, rd */
/* emul dsp[rs], rd */
static bool trans_EMUL_rl(DisasContext *ctx, arg_EMUL_rl *a)
{
    prt("emul\n");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* emulu #imm, rd */
static bool trans_EMULU_ri(DisasContext *ctx, arg_EMULU_ri *a)
{
    prt("emulu\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* emulu rs, rd */
/* emulu dsp[rs], rd */
static bool trans_EMULU_rl(DisasContext *ctx, arg_EMULU_rl *a)
{
    prt("emulu\n");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* div #imm, rd */
static bool trans_DIV_ri(DisasContext *ctx, arg_DIV_ri *a)
{
    prt("div\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* div rs, rd */
/* div dsp[rs], rd */
static bool trans_DIV_rl(DisasContext *ctx, arg_DIV_rl *a)
{
    prt("div\n");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* divu #imm, rd */
static bool trans_DIVU_ri(DisasContext *ctx, arg_DIVU_ri *a)
{
    prt("divu\t#0x%08x,r%d", a->imm, a->rd);
    return true;
}

/* divu rs, rd */
/* divu dsp[rs], rd */
static bool trans_DIVU_rl(DisasContext *ctx, arg_DIVU_rl *a)
{
    prt("divu\n");
    operand(ctx, a->ld, a->mi, a->rs, a->rd);
    return true;
}


/* shll #imm:5, rd */
/* shll #imm:5, rs, rd */
static bool trans_SHLL_rri(DisasContext *ctx, arg_SHLL_rri *a)
{
    prt("shll\t#%d,", a->imm);
    if (a->rs2 < 16) {
        prt("r%d,", a->rs2);
    }
    prt("r%d", a->rd);
    return true;
}

/* shll rs, rd */
static bool trans_SHLL_rr(DisasContext *ctx, arg_SHLL_rr *a)
{
    prt("shll\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* shar #imm:5, rd */
/* shar #imm:5, rs, rd */
static bool trans_SHAR_rri(DisasContext *ctx, arg_SHAR_rri *a)
{
    prt("shar\t#%d,", a->imm);
    if (a->rs2 < 16) {
        prt("r%d,", a->rs2);
    }
    prt("r%d", a->rd);
    return true;
}

/* shar rs, rd */
static bool trans_SHAR_rr(DisasContext *ctx, arg_SHAR_rr *a)
{
    prt("shar\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* shlr #imm:5, rd */
/* shlr #imm:5, rs, rd */
static bool trans_SHLR_rri(DisasContext *ctx, arg_SHLR_rri *a)
{
    prt("shlr\t#%d,", a->imm);
    if (a->rs2 < 16) {
        prt("r%d,", a->rs2);
    }
    prt("r%d", a->rd);
    return true;
}

/* shlr rs, rd */
static bool trans_SHLR_rr(DisasContext *ctx, arg_SHLR_rr *a)
{
    prt("shlr\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* rolc rd */
static bool trans_ROLC(DisasContext *ctx, arg_ROLC *a)
{
    prt("rorc\tr%d", a->rd);
    return true;
}

/* rorc rd */
static bool trans_RORC(DisasContext *ctx, arg_RORC *a)
{
    prt("rorc\tr%d", a->rd);
    return true;
}

/* rotl #imm, rd */
static bool trans_ROTL_ri(DisasContext *ctx, arg_ROTL_ri *a)
{
    prt("rotl\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* rotl rs, rd */
static bool trans_ROTL_rr(DisasContext *ctx, arg_ROTL_rr *a)
{
    prt("rotl\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* rotr #imm, rd */
static bool trans_ROTR_ri(DisasContext *ctx, arg_ROTR_ri *a)
{
    prt("rotr\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* rotr rs, rd */
static bool trans_ROTR_rr(DisasContext *ctx, arg_ROTR_rr *a)
{
    prt("rotr\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* revl rs, rd */
static bool trans_REVL(DisasContext *ctx, arg_REVL *a)
{
    prt("revl\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* revw rs, rd */
static bool trans_REVW(DisasContext *ctx, arg_REVW *a)
{
    prt("revw\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* conditional branch helper */
static void rx_bcnd_main(DisasContext *ctx, int cd, int dst, int len)
{
    static const char sz[] = {'s', 'b', 'w', 'a'};
    prt("b%s.%c\t%08x", cond[cd], sz[len - 1], ctx->addr - len + dst);
}

static int16_t rev16(uint16_t dsp)
{
    return ((dsp << 8) & 0xff00) | ((dsp >> 8) & 0x00ff);
}

static int32_t rev24(uint32_t dsp)
{
    dsp = ((dsp << 16) & 0xff0000) |
        (dsp & 0x00ff00) |
        ((dsp >> 16) & 0x0000ff);
    dsp |= (dsp & 0x00800000) ? 0xff000000 : 0x00000000;
    return dsp;
}

/* beq dsp:3 */
/* bne dsp:3 */
static bool trans_BCnd_s(DisasContext *ctx, arg_BCnd_s *a)
{
    if (a->dsp < 3) {
        a->dsp += 8;
    }
    rx_bcnd_main(ctx, a->cd, a->dsp, 1);
    return true;
}

/* beq dsp:8 */
/* bne dsp:8 */
/* bc dsp:8 */
/* bnc dsp:8 */
/* bgtu dsp:8 */
/* bleu dsp:8 */
/* bpz dsp:8 */
/* bn dsp:8 */
/* bge dsp:8 */
/* blt dsp:8 */
/* bgt dsp:8 */
/* ble dsp:8 */
/* bo dsp:8 */
/* bno dsp:8 */
/* bra dsp:8 */
static bool trans_BCnd_b(DisasContext *ctx, arg_BCnd_b *a)
{
    rx_bcnd_main(ctx, a->cd, (int8_t)a->dsp, 2);
    return true;
}

/* beq dsp:16 */
/* bne dsp:16 */
static bool trans_BCnd_w(DisasContext *ctx, arg_BCnd_w *a)
{
    rx_bcnd_main(ctx, a->cd, rev16(a->dsp), 3);
    return true;
}

/* bra dsp:3 */
static bool trans_BRA_s(DisasContext *ctx, arg_BRA_s *a)
{
    if (a->dsp < 3) {
        a->dsp += 8;
    }
    rx_bcnd_main(ctx, 14, a->dsp, 1);
    return true;
}

/* bra dsp:16 */
static bool trans_BRA_w(DisasContext *ctx, arg_BRA_w *a)
{
    rx_bcnd_main(ctx, 14, rev16(a->dsp), 3);
    return true;
}

/* bra dsp:24 */
static bool trans_BRA_a(DisasContext *ctx, arg_BRA_a *a)
{
    rx_bcnd_main(ctx, 14, rev24(a->dsp), 4);
    return true;
}

/* bra rs */
static bool trans_BRA_l(DisasContext *ctx, arg_BRA_l *a)
{
    prt("bra.l\tr%d", a->rd);
    return true;
}

/* jmp rs */
static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    prt("jmp\tr%d", a->rs);
    return true;
}

/* jsr rs */
static bool trans_JSR(DisasContext *ctx, arg_JSR *a)
{
    prt("jsr\tr%d", a->rs);
    return true;
}

/* bsr dsp:16 */
static bool trans_BSR_w(DisasContext *ctx, arg_BSR_w *a)
{
    prt("bsr.w\t%08x", ctx->addr - 3 + rev16(a->dsp));
    return true;
}

/* bsr dsp:24 */
static bool trans_BSR_a(DisasContext *ctx, arg_BSR_a *a)
{
    prt("bsr.a\t%08x", ctx->addr - 4 + rev24(a->dsp));
    return true;
}

/* bsr rs */
static bool trans_BSR_l(DisasContext *ctx, arg_BSR_l *a)
{
    prt("bsr.l\tr%d", a->rd);
    return true;
}

/* rts */
static bool trans_RTS(DisasContext *ctx, arg_RTS *a)
{
    prt("rts");
    return true;
}

/* nop */
static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    prt("nop");
    return true;
}

/* scmpu */
static bool trans_SCMPU(DisasContext *ctx, arg_SCMPU *a)
{
    prt("scmpu");
    return true;
}

/* smovu */
static bool trans_SMOVU(DisasContext *ctx, arg_SMOVU *a)
{
    prt("smovu");
    return true;
}

/* smovf */
static bool trans_SMOVF(DisasContext *ctx, arg_SMOVF *a)
{
    prt("smovf");
    return true;
}

/* smovb */
static bool trans_SMOVB(DisasContext *ctx, arg_SMOVB *a)
{
    prt("smovb");
    return true;
}

/* suntile */
static bool trans_SUNTIL(DisasContext *ctx, arg_SUNTIL *a)
{
    prt("suntil.%c", size[a->sz]);
    return true;
}

/* swhile */
static bool trans_SWHILE(DisasContext *ctx, arg_SWHILE *a)
{
    prt("swhile.%c", size[a->sz]);
    return true;
}
/* sstr */
static bool trans_SSTR(DisasContext *ctx, arg_SSTR *a)
{
    prt("sstr.%c", size[a->sz]);
    return true;
}

/* rmpa */
static bool trans_RMPA(DisasContext *ctx, arg_RMPA *a)
{
    prt("rmpa.%c", size[a->sz]);
    return true;
}

/* mulhi rs,rs2 */
static bool trans_MULHI(DisasContext *ctx, arg_MULHI *a)
{
    prt("mulhi\tr%d,r%d", a->rs, a->rs2);
    return true;
}

/* mullo rs,rs2 */
static bool trans_MULLO(DisasContext *ctx, arg_MULLO *a)
{
    prt("mullo\tr%d,r%d", a->rs, a->rs2);
    return true;
}

/* machi rs,rs2 */
static bool trans_MACHI(DisasContext *ctx, arg_MACHI *a)
{
    prt("machi\tr%d,r%d", a->rs, a->rs2);
    return true;
}

/* maclo rs,rs2 */
static bool trans_MACLO(DisasContext *ctx, arg_MACLO *a)
{
    prt("maclo\tr%d,r%d", a->rs, a->rs2);
    return true;
}

/* mvfachi rd */
static bool trans_MVFACHI(DisasContext *ctx, arg_MVFACHI *a)
{
    prt("mvfachi\tr%d", a->rd);
    return true;
}

/* mvfacmi rd */
static bool trans_MVFACMI(DisasContext *ctx, arg_MVFACMI *a)
{
    prt("mvfacmi\tr%d", a->rd);
    return true;
}

/* mvtachi rs */
static bool trans_MVTACHI(DisasContext *ctx, arg_MVTACHI *a)
{
    prt("mvtachi\tr%d", a->rs);
    return true;
}

/* mvtaclo rs */
static bool trans_MVTACLO(DisasContext *ctx, arg_MVTACLO *a)
{
    prt("mvtaclo\tr%d", a->rs);
    return true;
}

/* racw #imm */
static bool trans_RACW(DisasContext *ctx, arg_RACW *a)
{
    prt("racw\t#%d", a->imm + 1);
    return true;
}

/* sat rd */
static bool trans_SAT(DisasContext *ctx, arg_SAT *a)
{
    prt("sat\tr%d", a->rd);
    return true;
}

/* satr */
static bool trans_SATR(DisasContext *ctx, arg_SATR *a)
{
    prt("satr");
    return true;
}

/* fadd #imm, rd */
static bool trans_FADD_ri(DisasContext *ctx, arg_FADD_ri *a)
{
    prt("fadd\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fadd dsp[rs], rd */
/* fadd rs, rd */
static bool trans_FADD_rl(DisasContext *ctx, arg_FADD_rl *a)
{
    prt("fadd\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* fcmp #imm, rd */
static bool trans_FCMP_ri(DisasContext *ctx, arg_FCMP_ri *a)
{
    prt("fadd\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fcmp dsp[rs], rd */
/* fcmp rs, rd */
static bool trans_FCMP_rl(DisasContext *ctx, arg_FCMP_rl *a)
{
    prt("fcmp\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* fsub #imm, rd */
static bool trans_FSUB_ri(DisasContext *ctx, arg_FSUB_ri *a)
{
    prt("fsub\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fsub dsp[rs], rd */
/* fsub rs, rd */
static bool trans_FSUB_rl(DisasContext *ctx, arg_FSUB_rl *a)
{
    prt("fsub\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* ftoi dsp[rs], rd */
/* ftoi rs, rd */
static bool trans_FTOI(DisasContext *ctx, arg_FTOI *a)
{
    prt("ftoi\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* fmul #imm, rd */
static bool trans_FMUL_ri(DisasContext *ctx, arg_FMUL_ri *a)
{
    prt("fmul\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fmul dsp[rs], rd */
/* fmul rs, rd */
static bool trans_FMUL_rl(DisasContext *ctx, arg_FMUL_rl *a)
{
    prt("fmul\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* fdiv #imm, rd */
static bool trans_FDIV_ri(DisasContext *ctx, arg_FDIV_ri *a)
{
    prt("fdiv\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fdiv dsp[rs], rd */
/* fdiv rs, rd */
static bool trans_FDIV_rl(DisasContext *ctx, arg_FDIV_rl *a)
{
    prt("fdiv\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* round dsp[rs], rd */
/* round rs, rd */
static bool trans_ROUND(DisasContext *ctx, arg_ROUND *a)
{
    prt("round\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

/* itof rs, rd */
/* itof dsp[rs], rd */
static bool trans_ITOF(DisasContext *ctx, arg_ITOF *a)
{
    prt("itof\t");
    operand(ctx, a->ld, RX_MI_LONG, a->rs, a->rd);
    return true;
}

#define BOP_IM(name, reg)                           \
    do {                                            \
        int dsp;                                    \
        prt("b%s\t#%d,", #name, a->imm);             \
        dsp = rx_index_addr(a->ld, RX_MEMORY_BYTE, ctx);        \
        if (dsp > 0) {                                          \
            prt("%d", dsp);                                     \
        }                                                       \
        prt("[r%d]", reg);                                      \
        return true;                                            \
    } while (0)

/* bset #imm, dsp[rd] */
static bool trans_BSET_li(DisasContext *ctx, arg_BSET_li *a)
{
    BOP_IM(bset, a->rs);
}

#define BOP_RM(name) \
    do {             \
        int dsp;               \
        prt("b%s\tr%d,", #name, a->rs2);        \
        switch (a->ld) {                        \
        case 0 ... 2:                                    \
            dsp = rx_index_addr(a->ld, RX_MEMORY_BYTE, ctx);    \
            if (dsp > 0) {                                      \
                prt("%d", dsp);                                 \
            }                                                   \
            prt("[r%d]", a->rs);                                \
            break;                                              \
        case 3:                                                 \
            prt("r%d", a->rs);                                  \
            break;                                              \
        }                                                       \
        return true;                                            \
    } while (0)

/* bset rs, dsp[rd] */
/* bset rs, rd */
static bool trans_BSET_lr(DisasContext *ctx, arg_BSET_lr *a)
{
    BOP_RM(set);
}

/* bset #imm, rd */
static bool trans_BSET_ri(DisasContext *ctx, arg_BSET_ri *a)
{
    prt("bset\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* bclr #imm, dsp[rd] */
static bool trans_BCLR_li(DisasContext *ctx, arg_BCLR_li *a)
{
BOP_IM(clr, a->rs);
}

/* bclr rs, dsp[rd] */
/* bclr rs, rd */
static bool trans_BCLR_lr(DisasContext *ctx, arg_BCLR_lr *a)
{
    BOP_RM(clr);
}

/* bclr #imm, rd */
static bool trans_BCLR_ri(DisasContext *ctx, arg_BCLR_ri *a)
{
    prt("bclr\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* btst #imm, dsp[rd] */
static bool trans_BTST_li(DisasContext *ctx, arg_BTST_li *a)
{
    BOP_IM(tst, a->rs);
}

/* btst rs, dsp[rd] */
/* btst rs, rd */
static bool trans_BTST_lr(DisasContext *ctx, arg_BTST_lr *a)
{
    BOP_RM(tst);
}

/* btst #imm, rd */
static bool trans_BTST_ri(DisasContext *ctx, arg_BTST_ri *a)
{
    prt("btst\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* bnot rs, dsp[rd] */
/* bnot rs, rd */
static bool trans_BNOT_lr(DisasContext *ctx, arg_BNOT_lr *a)
{
    BOP_RM(not);
}

/* bmcond #imm, dsp[rd] */
/* bnot #imm, dsp[rd] */
static bool trans_BMCnd_BNOT_mi(DisasContext *ctx, arg_BMCnd_BNOT_mi *a)
{
    if (a->cd == 15) {
        BOP_IM(not, a->rd);
    } else {
        int dsp = rx_index_addr(a->ld, RX_MEMORY_BYTE, ctx);
        prt("bm%s\t#%d,", cond[a->cd], a->imm);
        if (dsp > 0) {
            prt("%d", dsp);
        }
        prt("[%d]", a->rd);
    }
    return true;
}

/* bmcond #imm, rd */
/* bnot #imm, rd */
static bool trans_BMCnd_BNOT_ri(DisasContext *ctx, arg_BMCnd_BNOT_ri *a)
{
    if (a->cd == 15) {
        prt("bnot\t#%d,r%d", a->imm, a->rd);
    } else {
        prt("bm%s\t#%d,r%d", cond[a->cd], a->imm, a->rd);
    }
    return true;
}

/* clrpsw psw */
static bool trans_CLRPSW(DisasContext *ctx, arg_CLRPSW *a)
{
    prt("clrpsw\t%c", psw[a->cb]);
    return true;
}

/* setpsw psw */
static bool trans_SETPSW(DisasContext *ctx, arg_SETPSW *a)
{
    prt("setpsw\t%c", psw[a->cb]);
    return true;
}

/* mvtipl #imm */
static bool trans_MVTIPL(DisasContext *ctx, arg_MVTIPL *a)
{
    prt("movtipl\t#%d", a->imm);
    return true;
}

/* mvtc #imm, rd */
static bool trans_MVTC_i(DisasContext *ctx, arg_MVTC_i *a)
{
    prt("mvtc/t#0x%08x,%s", a->imm, cr[a->cr]);
    return true;
}

/* mvtc rs, rd */
static bool trans_MVTC_r(DisasContext *ctx, arg_MVTC_r *a)
{
    prt("mvtc/tr%d,%s", a->rs, cr[a->cr]);
    return true;
}

/* mvfc rs, rd */
static bool trans_MVFC(DisasContext *ctx, arg_MVFC *a)
{
    prt("mvfc/t%s,r%d", cr[a->cr], a->rd);
    return true;
}

/* rtfi */
static bool trans_RTFI(DisasContext *ctx, arg_RTFI *a)
{
    prt("rtfi");
    return true;
}

/* rte */
static bool trans_RTE(DisasContext *ctx, arg_RTE *a)
{
    prt("rte");
    return true;
}

/* brk */
static bool trans_BRK(DisasContext *ctx, arg_BRK *a)
{
    prt("brk");
    return true;
}

/* int #imm */
static bool trans_INT(DisasContext *ctx, arg_INT *a)
{
    prt("int\t#%d", a->imm);
    return true;
}

/* wait */
static bool trans_WAIT(DisasContext *ctx, arg_WAIT *a)
{
    prt("wait");
    return true;
}

/* sccnd.[bwl] rd */
/* sccnd.[bwl] dsp:[rd] */
static bool trans_SCCnd(DisasContext *ctx, arg_SCCnd *a)
{
    int dsp;
    prt("sc%s.%c\t", cond[a->cd], size[a->sz]);
    if (a->ld < 3) {
        dsp = rx_index_addr(a->sz, a->ld, ctx);
        if (dsp > 0) {
            prt("%d", dsp);
        }
        prt("[r%d]", a->rd);
    } else {
        prt("r%d", a->rd);
    }
    return true;
}

int print_insn_rx(bfd_vma addr, disassemble_info *dis)
{
    DisasContext ctx;
    uint32_t insn;
    int i;
    ctx.dis = dis;
    ctx.addr = addr;

    insn = decode_load(&ctx);
    if (!decode(&ctx, insn)) {
        ctx.dis->fprintf_func(ctx.dis->stream, ".byte\t");
        for (i = 0; i < ctx.addr - addr; i++) {
            if (i > 0) {
                ctx.dis->fprintf_func(ctx.dis->stream, ",");
            }
            ctx.dis->fprintf_func(ctx.dis->stream, "0x%02x", insn >> 24);
            insn <<= 8;
        }
    }
    return ctx.addr - addr;
}
