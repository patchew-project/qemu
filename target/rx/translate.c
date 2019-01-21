/*
 *  RX translation
 *
 *  Copyright (c) 2019 Yoshinori Sato
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "trace-tcg.h"
#include "exec/log.h"

typedef struct DisasContext {
    DisasContextBase base;
    uint32_t pc;
} DisasContext;

/* PSW condition operation */
typedef struct {
    TCGv op_mode;
    TCGv op_a1[13];
    TCGv op_a2[13];
    TCGv op_r[13];
} CCOP;
CCOP ccop;

/* Target-specific values for dc->base.is_jmp.  */
#define DISAS_JUMP    DISAS_TARGET_0

/* global register indexes */
static TCGv cpu_regs[16];
static TCGv cpu_psw, cpu_psw_o, cpu_psw_s, cpu_psw_z, cpu_psw_c;
static TCGv cpu_psw_i, cpu_psw_pm, cpu_psw_u, cpu_psw_ipl;
static TCGv cpu_usp, cpu_fpsw, cpu_bpsw, cpu_bpc, cpu_isp;
static TCGv cpu_fintv, cpu_intb, cpu_pc, cpu_acc_m, cpu_acc_l;


#include "exec/gen-icount.h"

void rx_cpu_dump_state(CPUState *cs, FILE *f,
                           fprintf_function cpu_fprintf, int flags)
{
    RXCPU *cpu = RXCPU(cs);
    CPURXState *env = &cpu->env;
    int i;
    uint32_t psw;

    psw = rx_get_psw_low(env);
    psw |= (env->psw_ipl << 24) | (env->psw_pm << 20) |
        (env->psw_u << 17) | (env->psw_i << 16);
    cpu_fprintf(f, "pc=0x%08x psw=0x%08x\n",
                env->pc, psw);
    for (i = 0; i < 16; i += 4) {
        cpu_fprintf(f, "r%d=0x%08x r%d=0x%08x r%d=0x%08x r%d=0x%08x\n",
                    i, env->regs[i], i + 1, env->regs[i + 1],
                    i + 2, env->regs[i + 2], i + 3, env->regs[i + 3]);
    }
}

static inline void gen_save_cpu_state(DisasContext *dc, bool save_pc)
{
    if (save_pc) {
        tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
    }
}

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
    if (unlikely(dc->base.singlestep_enabled)) {
        return false;
    } else {
        return true;
    }
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    if (use_goto_tb(dc, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(dc->base.tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        if (dc->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

typedef void (*disas_proc)(CPURXState *env, DisasContext *dc,
                           uint32_t insn);

static uint32_t rx_load_simm(CPURXState *env, uint32_t addr,
                             int sz, uint32_t *ret)
{
    int32_t tmp;
    switch (sz) {
    case 1:
        *ret = cpu_ldsb_code(env, addr);
        return addr + 1;
    case 2:
        *ret = cpu_ldsw_code(env, addr);
        return addr + 2;
    case 3:
        tmp = cpu_ldsb_code(env, addr + 2) << 16;
        tmp |= cpu_lduw_code(env, addr) & 0xffff;
        *ret = tmp;
        return addr + 3;
    case 0:
        *ret = cpu_ldl_code(env, addr);
        return addr + 4;
    default:
        return addr;
    }
}

#define SET_MODE_O(mode)                                                \
    do {                                                                \
        tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, ~0xf000);          \
        tcg_gen_ori_i32(ccop.op_mode, ccop.op_mode, mode << 12);        \
    } while (0)

#define SET_MODE_ZS(mode)                                               \
    do {                                                                \
        tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, ~0x0ff0);          \
        tcg_gen_ori_i32(ccop.op_mode, ccop.op_mode,                     \
                        (mode << 8) | (mode << 4));                     \
    } while (0)

#define SET_MODE_ZSO(mode)                                              \
    do {                                                                \
        tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, ~0xfff0);          \
        tcg_gen_ori_i32(ccop.op_mode, ccop.op_mode,                     \
                        (mode << 12) | (mode << 8) | (mode << 4));      \
    } while (0)

#define SET_MODE_CZ(mode)                                               \
    do {                                                                \
        tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, ~0x00ff);          \
        tcg_gen_ori_i32(ccop.op_mode, ccop.op_mode,                     \
                        (mode << 4) | mode);                            \
    } while (0)

#define SET_MODE_CZSO(mode)                                             \
    do {                                                                \
        tcg_gen_movi_i32(ccop.op_mode,                                  \
                         (mode << 12) | (mode << 8) |                   \
                         (mode << 4) | mode);                           \
    } while (0)

#define SET_MODE_CZS(mode)                                              \
    do {                                                                \
        tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, ~0x0fff);          \
        tcg_gen_ori_i32(ccop.op_mode, ccop.op_mode,                     \
                        (mode << 8) | (mode << 4) | mode);              \
    } while (0)

#define DEFINE_INSN(name) \
    static void name(CPURXState *env, DisasContext *dc, uint32_t insn)

#define RX_MEMORY_ST 0
#define RX_MEMORY_LD 1
#define RX_MEMORY_BYTE 0
#define RX_MEMORY_WORD 1
#define RX_MEMORY_LONG 2

#define RX_OP_SUB 0
#define RX_OP_CMP 1
#define RX_OP_ADD 2
#define RX_OP_SBB 3
#define RX_OP_ADC 4
#define RX_OP_MUL 3

static void rx_gen_ldst(int size, int dir, TCGv reg, TCGv mem)
{
    static void (* const rw[])(TCGv ret, TCGv addr, int idx) = {
        tcg_gen_qemu_st8, tcg_gen_qemu_ld8s,
        tcg_gen_qemu_st16, tcg_gen_qemu_ld16s,
        tcg_gen_qemu_st32, tcg_gen_qemu_ld32s,
    };
    rw[size * 2 + dir](reg, mem, 0);
}

/* mov.[bwl] rs,dsp:[rd] / mov.[bwl] dsp:[rs],rd */
DEFINE_INSN(mov1_2)
{
    TCGv mem;
    int r1, r2, dsp, dir, sz;

    insn >>= 16;
    sz = (insn >> 12) & 3;
    dsp = ((insn >> 6) & 0x1e) | ((insn >> 3) & 1);
    dsp <<= sz;
    r2 = insn & 7;
    r1 = (insn >> 4) & 7;
    dir = (insn >> 11) & 1;

    mem = tcg_temp_local_new();
    tcg_gen_addi_i32(mem, cpu_regs[r1], dsp);
    rx_gen_ldst(sz, dir, cpu_regs[r2], mem);
    tcg_temp_free(mem);
    dc->pc += 2;
}

/* mov.l #uimm:4,rd */
DEFINE_INSN(mov3)
{
    uint32_t imm;
    int rd;

    imm = (insn >> 20) & 0x0f;
    rd = (insn >> 16) & 15;
    tcg_gen_movi_i32(cpu_regs[rd], imm);
    dc->pc += 2;
}

/* mov.[bwl] #imm8,dsp:[rd] */
DEFINE_INSN(mov4)
{
    uint32_t imm8;
    TCGv src, dst;
    int rd, sz, dsp;

    sz = (insn >> 24) & 3;
    rd = (insn >> 20) & 7;
    dsp = ((insn >> 19) & 0x10) | ((insn >> 16) & 0x0f);
    dsp <<= sz;
    imm8 = (insn >> 8) & 0xff;

    src = tcg_const_local_i32(imm8);
    dst = tcg_temp_local_new();
    tcg_gen_addi_i32(dst, cpu_regs[rd], dsp);
    rx_gen_ldst(sz, RX_MEMORY_ST, src, dst);
    tcg_temp_free(src);
    tcg_temp_free(dst);
    dc->pc += 3;
}

/* mov.l #uimm8,rd */
DEFINE_INSN(mov5)
{
    uint32_t imm8;
    int rd;

    imm8 = (insn >> 8) & 0xff;
    rd = (insn >> 16) & 15;
    tcg_gen_movi_i32(cpu_regs[rd], imm8);
    dc->pc += 3;
}

/* mov.l #imm,rd */
DEFINE_INSN(mov6)
{
    uint32_t imm;
    int rd, li;

    rd = (insn >> 20) & 15;
    li = (insn >> 18) & 3;

    dc->pc = rx_load_simm(env, dc->pc + 2, li, &imm);
    tcg_gen_movi_i32(cpu_regs[rd], imm);
}

/* mov.[bwl] rs,rd */
DEFINE_INSN(mov7)
{
    int sz, rs, rd;

    sz = (insn >> 28) & 3;
    rs = (insn >> 20) & 15;
    rd = (insn >> 16) & 15;

    switch (sz) {
    case 0:
        tcg_gen_ext8s_i32(cpu_regs[rd], cpu_regs[rs]);
        break;
    case 1:
        tcg_gen_ext16s_i32(cpu_regs[rd], cpu_regs[rs]);
        break;
    case 2:
        tcg_gen_mov_i32(cpu_regs[rd], cpu_regs[rs]);
        break;
    }
    dc->pc += 2;
}

static TCGv rx_index_addr(int id, int size, int offset, int reg,
                          DisasContext *dc, CPURXState *env)
{
    TCGv addr;
    uint32_t dsp;

    addr = tcg_temp_local_new();
    switch (id) {
    case 0:
        tcg_gen_mov_i32(addr, cpu_regs[reg]);
        break;
    case 1:
        dsp = cpu_ldub_code(env, dc->base.pc_next + offset) << size;
        tcg_gen_addi_i32(addr, cpu_regs[reg], dsp);
        break;
    case 2:
        dsp = cpu_lduw_code(env, dc->base.pc_next + offset) << size;
        tcg_gen_addi_i32(addr, cpu_regs[reg], dsp);
        break;
    }
    return addr;
}

/* mov #imm, dsp:[rd] */
DEFINE_INSN(mov8)
{
    uint32_t imm;
    TCGv _imm, dst;
    int id, rd, li, sz;

    id = (insn >> 24) & 3;
    rd = (insn >> 20) & 15;
    li = (insn >> 18) & 3;
    sz = (insn >> 16) & 3;

    dst = rx_index_addr(id, sz, 2, rd, dc, env);
    dc->pc = rx_load_simm(env, dc->pc + 2 + id, li, &imm);
    _imm = tcg_const_local_i32(imm);
    rx_gen_ldst(sz, RX_MEMORY_ST, _imm, dst);
    tcg_temp_free(_imm);
    tcg_temp_free(dst);
}

/* mov.[bwl] dsp:[rs],rd */
DEFINE_INSN(mov9)
{
    int sz, id, rs, rd;
    TCGv src;

    sz = (insn >> 28) & 3;
    id = (insn >> 24) & 3;
    rs = (insn >> 20) & 15;
    rd = (insn >> 16) & 15;

    src = rx_index_addr(id, sz, 2, rs, dc, env);
    rx_gen_ldst(sz, RX_MEMORY_LD, cpu_regs[rd], src);
    tcg_temp_free(src);
    dc->pc += 2 + id;
}

static TCGv rx_gen_regindex(int size, int ri, int rb)
{
    TCGv ret;

    ret = tcg_temp_local_new();
    tcg_gen_shli_i32(ret, cpu_regs[ri], size);
    tcg_gen_add_i32(ret, ret, cpu_regs[rb]);
    return ret;
}

/* mov.[bwl] [ri,rb],rd / mov.[bwl] rd,[ri,rb] */
DEFINE_INSN(mov10_12)
{
    TCGv mem;
    int sz, ri, rb, rn, dir;

    dir = (insn >> 22) & 1;
    sz = (insn >> 20) & 3;
    ri = (insn >> 16) & 15;
    rb = (insn >> 12) & 15;
    rn = (insn >> 8) & 15;

    mem = rx_gen_regindex(sz, ri, rb);
    rx_gen_ldst(sz, dir, cpu_regs[rn], mem);
    tcg_temp_free(mem);
    dc->pc += 3;
}

/* mov.[bwl] rs,dsp:[rd] */
DEFINE_INSN(mov11)
{
    int sz, id, rs, rd;
    TCGv mem;

    sz = (insn >> 28) & 3;
    id = (insn >> 26) & 3;
    rd = (insn >> 20) & 15;
    rs = (insn >> 16) & 15;

    mem = rx_index_addr(id, sz, 2, rd, dc, env);
    rx_gen_ldst(sz, RX_MEMORY_ST, cpu_regs[rs], mem);
    tcg_temp_free(mem);
    dc->pc += 2 + id;
}

/* mov.[bwl] dsp:[rs],dsp:[rd] */
DEFINE_INSN(mov13)
{
    int sz, rs, rd, ids, idd;
    TCGv src, dst, val;

    sz = (insn >> 28) & 3;
    idd = (insn >> 26) & 3;
    ids = (insn >> 24) & 3;
    rs = (insn >> 20) & 15;
    rd = (insn >> 16) & 15;

    src = rx_index_addr(ids, sz, 2, rs, dc, env);
    dst = rx_index_addr(idd, sz, 2 + ids, rd, dc, env);
    val = tcg_temp_local_new();
    rx_gen_ldst(sz, RX_MEMORY_LD, val, src);
    rx_gen_ldst(sz, RX_MEMORY_ST, val, dst);
    tcg_temp_free(src);
    tcg_temp_free(dst);
    tcg_temp_free(val);
    dc->pc += 2 + ids + idd;
}

/* mov.[bwl] rs,[rd+] / mov.[bwl] rs,[-rd] */
DEFINE_INSN(mov14)
{
    int rs, rd, ad, sz;
    TCGv dst;

    ad = (insn >> 18) & 3;
    sz = (insn >> 16) & 3;
    rd = (insn >> 12) & 15;
    rs = (insn >> 8) & 15;

    dst = tcg_temp_local_new();
    tcg_gen_mov_i32(dst, cpu_regs[rd]);
    if (ad == 1) {
        tcg_gen_subi_i32(dst, dst, 1 << sz);
    }
    rx_gen_ldst(sz, RX_MEMORY_ST, cpu_regs[rs], dst);
    if (ad == 0) {
        tcg_gen_addi_i32(cpu_regs[rd], cpu_regs[rd], 1 << sz);
    } else {
        tcg_gen_mov_i32(cpu_regs[rd], dst);
    }
    tcg_temp_free(dst);
    dc->pc += 3;
}

/* mov.[bwl] [rs+],rd / mov.[bwl] [-rs],rd */
DEFINE_INSN(mov15)
{
    int rs, rd, ad, sz;

    ad = (insn >> 18) & 3;
    sz = (insn >> 16) & 3;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    if (ad == 3) {
        tcg_gen_subi_i32(cpu_regs[rs], cpu_regs[rs], 1 << sz);
    }
    rx_gen_ldst(sz, RX_MEMORY_LD, cpu_regs[rd], cpu_regs[rs]);
    if (ad == 2) {
        tcg_gen_addi_i32(cpu_regs[rs], cpu_regs[rs], 1 << sz);
    }
    dc->pc += 3;
}

static void rx_gen_ldu(unsigned int sz, TCGv reg, TCGv addr)
{
    static void (* const rd[])(TCGv ret, TCGv addr, int idx) = {
        tcg_gen_qemu_ld8u, tcg_gen_qemu_ld16u, tcg_gen_qemu_ld32u,
    };
    g_assert(sz < 3);
    rd[sz](reg, addr, 0);
}

/* movu.[bw] dsp5:[rs],rd */
DEFINE_INSN(movu1)
{
    int sz, dsp, rs, rd;
    TCGv mem;

    mem = tcg_temp_local_new();
    sz = (insn >> 27) & 1;
    dsp = ((insn >> 22) & 0x1e) | ((insn >> 19) & 1);
    rs = (insn >> 20) & 7;
    rd = (insn >> 16) & 7;

    tcg_gen_addi_i32(mem, cpu_regs[rs], dsp << sz);
    rx_gen_ldu(sz, cpu_regs[rd], mem);
    tcg_temp_free(mem);
    dc->pc += 2;
}

/* movu.[bw] rs,rd / movu.[bw] dsp:[rs],rd */
DEFINE_INSN(movu2)
{
    int sz, id, rs, rd;
    TCGv mem;
    static void (* const ext[])(TCGv ret, TCGv arg) = {
        tcg_gen_ext8u_i32, tcg_gen_ext16u_i32,
    };

    sz = (insn >> 26) & 1;
    id = (insn >> 24) & 3;
    rs = (insn >> 20) & 15;
    rd = (insn >> 16) & 15;

    if (id < 3) {
        mem = rx_index_addr(id, sz, 2, rs, dc, env);
        rx_gen_ldu(sz, cpu_regs[rd], mem);
        tcg_temp_free(mem);
        dc->pc += 2 + id;
    } else {
        ext[sz](cpu_regs[rd], cpu_regs[rs]);
        dc->pc += 2;
    }
}

/* movu.[bw] [ri,rb],rd */
DEFINE_INSN(movu3)
{
    TCGv mem;
    int sz, ri, rb, rd;

    sz = (insn >> 20) & 1;
    ri = (insn >> 16) & 15;
    rb = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    mem = rx_gen_regindex(sz, ri, rb);
    rx_gen_ldu(sz, cpu_regs[rd], mem);
    tcg_temp_free(mem);
    dc->pc += 3;
}

/* movu.[bw] [rs+],rd / movu.[bw] [-rs],rd */
DEFINE_INSN(movu4)
{
    int rs, rd, ad, sz;

    ad = (insn >> 18) & 3;
    sz = (insn >> 16) & 1;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    if (ad == 3) {
        tcg_gen_subi_i32(cpu_regs[rs], cpu_regs[rs], 1 << sz);
    }
    rx_gen_ldu(sz, cpu_regs[rd], cpu_regs[rs]);
    if (ad == 2) {
        tcg_gen_addi_i32(cpu_regs[rs], cpu_regs[rs], 1 << sz);
    }
    dc->pc += 3;
}

/* pop rd */
DEFINE_INSN(pop)
{
    int rd;

    rd = (insn >> 16) & 15;
    tcg_gen_qemu_ld32u(cpu_regs[rd], cpu_regs[0], 0);
    if (rd != 0) {
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    }
    dc->pc += 2;
}

/* popc rx */
DEFINE_INSN(popc)
{
    TCGv cr, val;

    cr = tcg_const_i32((insn >> 16) & 15);
    val = tcg_temp_local_new();
    tcg_gen_qemu_ld32u(val, cpu_regs[0], 0);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    gen_helper_mvtc(cpu_env, cr, val);
    tcg_temp_free(cr);
    tcg_temp_free(val);
    dc->pc += 2;
}

/* popm rd-rd2 */
DEFINE_INSN(popm)
{
    int rd, rd2, r;

    rd = (insn >> 20) & 15;
    rd2 = (insn >> 16) & 15;

    for (r = rd; r <= rd2; r++) {
        tcg_gen_qemu_ld32u(cpu_regs[r], cpu_regs[0], 0);
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    }
    dc->pc += 2;
}

/* push rs */
DEFINE_INSN(push1)
{
    TCGv tmp;
    int rs;

    rs = (insn >> 16) & 15;
    tmp = tcg_temp_local_new();
    tcg_gen_mov_i32(tmp, cpu_regs[rs]);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    tcg_gen_qemu_st32(tmp, cpu_regs[0], 0);
    tcg_temp_free(tmp);
    dc->pc += 2;
}

/* push rs */
DEFINE_INSN(push2)
{
    TCGv tmp, mem;
    int id, sz, rs;

    id = (insn >> 24) & 3;
    rs = (insn >> 20) & 15;
    sz = (insn >> 16) & 3;
    tmp = tcg_temp_local_new();
    mem = rx_index_addr(id, sz, 2, rs, dc, env);
    rx_gen_ldst(sz, RX_MEMORY_LD, tmp, mem);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    tcg_gen_qemu_st32(tmp, cpu_regs[0], 0);
    tcg_temp_free(tmp);
    tcg_temp_free(mem);
    dc->pc += 2 + id;
}

/* pushc rx */
DEFINE_INSN(pushc)
{
    TCGv cr, val;

    cr = tcg_const_i32((insn >> 16) & 15);
    val = tcg_temp_local_new();
    gen_helper_mvfc(val, cpu_env, cr);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    tcg_gen_qemu_st32(val, cpu_regs[0], 0);
    tcg_temp_free(cr);
    tcg_temp_free(val);
    dc->pc += 2;
}

    /* pushm */
DEFINE_INSN(pushm)
{
    int rs, rs2, r;

    rs = (insn >> 20) & 15;
    rs2 = (insn >> 16) & 15;

    for (r = rs2; r >= rs; r--) {
        tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
        tcg_gen_qemu_st32(cpu_regs[r], cpu_regs[0], 0);
    }
    dc->pc += 2;
}

/* revl rs, rd */
DEFINE_INSN(revl)
{
    int rs, rd;
    TCGv t0, t1;

    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();
    tcg_gen_rotri_i32(t0, cpu_regs[rs], 8);
    tcg_gen_andi_i32(t1, t0, 0xff000000);
    tcg_gen_shli_i32(t0, cpu_regs[rs], 8);
    tcg_gen_andi_i32(t0, t0, 0x00ff0000);
    tcg_gen_or_i32(t1, t1, t0);
    tcg_gen_shri_i32(t0, cpu_regs[rs], 8);
    tcg_gen_andi_i32(t0, t0, 0x0000ff00);
    tcg_gen_or_i32(t1, t1, t0);
    tcg_gen_rotli_i32(t0, cpu_regs[rs], 8);
    tcg_gen_ext8u_i32(t0, t0);
    tcg_gen_or_i32(cpu_regs[rd], t1, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    dc->pc += 3;
}

/* revw rs, rd */
DEFINE_INSN(revw)
{
    int rs, rd;
    TCGv t0, t1, t2;

    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();
    t2 = tcg_temp_local_new();
    tcg_gen_ext8u_i32(t0, cpu_regs[rs]);
    tcg_gen_shli_i32(t0, t0, 8);
    tcg_gen_shri_i32(t1, cpu_regs[rs], 8);
    tcg_gen_andi_i32(t1, t1, 0x000000ff);
    tcg_gen_or_i32(t2, t0, t1);
    tcg_gen_shli_i32(t0, cpu_regs[rs], 8);
    tcg_gen_andi_i32(t0, t0, 0xff000000);
    tcg_gen_shri_i32(t1, cpu_regs[rs], 8);
    tcg_gen_andi_i32(t1, t1, 0x00ff0000);
    tcg_gen_or_i32(t0, t0, t1);
    tcg_gen_or_i32(cpu_regs[rd], t2, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    dc->pc += 3;
}

DEFINE_INSN(sccnd)
{
    int sz, id, rd;
    TCGv result, cd;
    TCGv mem;
    sz = (insn >> 18) & 3;
    id = (insn >> 16) & 3;
    rd = (insn >> 12) & 15;
    cd = tcg_const_local_i32((insn >> 8) & 15);
    result = tcg_temp_local_new();

    gen_helper_cond(result, cpu_env, cd);
    if (id < 3) {
        mem = rx_index_addr(sz, id, 3, rd, dc, env);
        rx_gen_ldst(sz, RX_MEMORY_ST, result, mem);
        tcg_temp_free(mem);
        dc->pc += 3 + id;
    } else {
        tcg_gen_mov_i32(cpu_regs[rd], result);
        dc->pc += 3;
    }
    tcg_temp_free(result);
    tcg_temp_free(cd);
}

/* stz #imm,rd / stnz #imm,rd */
DEFINE_INSN(stz)
{
    int rd, li;
    uint32_t imm;
    TCGv zero, _imm, cond, result;
    li = (insn >> 18) & 3;
    cond = tcg_const_local_i32((insn >> 12) & 1);
    rd = (insn >> 8) & 15;
    result = tcg_temp_local_new();
    dc->pc = rx_load_simm(env, dc->pc + 3, li, &imm);
    _imm = tcg_const_local_i32(imm);
    gen_helper_cond(result, cpu_env, cond);
    zero = tcg_const_local_i32(0);
    tcg_gen_movcond_i32(TCG_COND_NE, cpu_regs[rd],
                        result, zero, _imm, cpu_regs[rd]);
    tcg_temp_free(zero);
    tcg_temp_free(_imm);
    tcg_temp_free(cond);
    tcg_temp_free(result);
}

DEFINE_INSN(xchg1)
{
    int id, rs, rd;
    TCGv tmp, mem;

    id = (insn >> 16) & 3;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    tmp = tcg_temp_local_new();
    if (id == 3) {
        tcg_gen_mov_i32(tmp, cpu_regs[rs]);
        tcg_gen_mov_i32(cpu_regs[rs], cpu_regs[rd]);
        tcg_gen_mov_i32(cpu_regs[rd], tmp);
        dc->pc += 3;
    } else {
        mem = rx_index_addr(id, RX_MEMORY_BYTE, 3, rs, dc, env);
        rx_gen_ldu(RX_MEMORY_BYTE, tmp, mem);
        rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, cpu_regs[rd], mem);
        tcg_gen_mov_i32(cpu_regs[rd], tmp);
        dc->pc += 3 + id;
        tcg_temp_free(mem);
    }
    tcg_temp_free(tmp);
}

DEFINE_INSN(xchg2)
{
    int id, rs, rd, sz, mi;
    TCGv tmp, mem;

    mi = (insn >> 22) & 3;
    id = (insn >> 16) & 3;
    rs = (insn >> 4) & 15;
    rd = insn & 15;
    sz = (mi < 3) ? mi : RX_MEMORY_WORD;

    tmp = tcg_temp_local_new();
    mem = rx_index_addr(id, sz, 4, rs, dc, env);
    if (mi == 3) {
        rx_gen_ldu(RX_MEMORY_WORD, tmp, mem);
    } else {
        rx_gen_ldst(sz, RX_MEMORY_LD, tmp, mem);
    }
    rx_gen_ldst(sz, RX_MEMORY_ST, cpu_regs[rd], mem);
    tcg_gen_mov_i32(cpu_regs[rd], tmp);
    dc->pc += 4 + id;
    tcg_temp_free(mem);
    tcg_temp_free(tmp);
}

static void rx_gen_logic(int opr, TCGv ret, TCGv r1, TCGv r2)
{
    static void (*fn[])(TCGv ret, TCGv arg1, TCGv arg2) = {
        tcg_gen_and_i32,
        tcg_gen_or_i32,
        tcg_gen_xor_i32,
        tcg_gen_and_i32,
    };
    fn[opr](ccop.op_r[RX_PSW_OP_LOGIC], r1, r2);
    SET_MODE_ZS(RX_PSW_OP_LOGIC);
    if (opr < 3) {
        tcg_gen_mov_i32(ret, ccop.op_r[RX_PSW_OP_LOGIC]);
    }
}

DEFINE_INSN(nop)
{
    dc->pc += 1;
}

static void rx_gen_logici(int opr, TCGv ret, TCGv r1, uint32_t imm)
{
    static void (*fn[])(TCGv ret, TCGv arg1, int arg2) = {
        tcg_gen_andi_i32,
        tcg_gen_ori_i32,
        tcg_gen_xori_i32,
        tcg_gen_andi_i32,
    };
    fn[opr](ccop.op_r[RX_PSW_OP_LOGIC], r1, imm);
    SET_MODE_ZS(RX_PSW_OP_LOGIC);
    if (opr < 3) {
        tcg_gen_mov_i32(ret, ccop.op_r[RX_PSW_OP_LOGIC]);
    }
}

#define UIMM4OP(opmask,  operation_fn)                          \
    do {                                                        \
        int op, rd;                                             \
        uint32_t imm;                                           \
        op = (insn >> 24) & opmask;                             \
        imm = (insn >> 20) & 15;                                \
        rd = (insn >> 16) & 15;                                 \
        operation_fn(op, cpu_regs[rd], cpu_regs[rd], imm);      \
        dc->pc += 2;                                            \
    } while (0)


/* and #uimm:4,rd / or #uimm:4,rd */
DEFINE_INSN(logic_op1)
{
    UIMM4OP(1, rx_gen_logici);
}

#define SIMMOP_S(operation_fn)                                          \
    do {                                                                \
        int op, rd, li;                                                 \
        uint32_t imm;                                                   \
        li = (insn >> 24) & 3;                                          \
        op = (insn >> 20) & 1;                                          \
        rd = (insn >> 16) & 15;                                         \
        dc->pc = rx_load_simm(env, dc->pc + 2, li, &imm);     \
        operation_fn;                                                   \
    } while (0)

#define SIMMOP_L(operation_fn)                                          \
    do {                                                                \
        int op, rd, li;                                                 \
        uint32_t imm;                                                   \
        li = (insn >> 18) & 3;                                          \
        op = (insn >> 12) & 1;                                          \
        rd = (insn >> 8) & 15;                                          \
        dc->pc = rx_load_simm(env, dc->pc + 3, li, &imm);               \
        operation_fn;                                                   \
    } while (0)

/* and #imm, rd / or #imm,rd / xor #imm,rd / tst #imm,rd */
DEFINE_INSN(logic_op2)
{
    if ((insn & 0xfc000000) == 0x74000000) {
        /* and / or */
        SIMMOP_S(rx_gen_logici(op, cpu_regs[rd], cpu_regs[rd], imm));
    } else if ((insn & 0x0000e000) == 0x0000c000) {
        /* xor / tst */
        SIMMOP_L(rx_gen_logici(3 - op, cpu_regs[rd], cpu_regs[rd], imm));
    } else
        g_assert_not_reached();

}

#define MEMOP1_S(opmask, operation_fn)                                  \
    do {                                                                \
        int op, id, rs, rd;                                             \
        TCGv mem, val;                                                  \
        op = (insn >> 26) & opmask;                                     \
        id = (insn >> 24) & 3;                                          \
        rs = (insn >> 20) & 15;                                         \
        rd = (insn >> 16) & 15;                                         \
        if (id == 3) {                                                  \
            operation_fn(op, cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]); \
            dc->pc += 2;                                                \
        } else {                                                        \
            mem = rx_index_addr(id, RX_MEMORY_BYTE, 2, rs, dc, env);    \
            val = tcg_temp_local_new();                                 \
            rx_gen_ldu(RX_MEMORY_BYTE, val, mem);                       \
            operation_fn(op, cpu_regs[rd], cpu_regs[rd], val);          \
            tcg_temp_free(mem);                                         \
            tcg_temp_free(val);                                         \
            dc->pc += 2 + id;                                           \
        }                                                               \
    } while (0)

#define MEMOP1_L(operation_fn_reg, operation_fn_mem)                    \
    do {                                                                \
        int op, id, rs, rd;                                             \
        TCGv mem, val;                                                  \
        op = (insn >> 18) & 1;                                          \
        id = (insn >> 16) & 3;                                          \
        rs = (insn >> 12) & 15;                                         \
        rd = (insn >> 8) & 15;                                          \
        if (id == 3) {                                                  \
            operation_fn_reg;                                           \
            dc->pc += 3;                                                \
        } else {                                                        \
            mem = rx_index_addr(id, 1, 3, rs, dc, env);                 \
            val = tcg_temp_local_new();                                 \
            rx_gen_ldu(RX_MEMORY_BYTE, val, mem);                       \
            operation_fn_mem;                                           \
            tcg_temp_free(mem);                                         \
            tcg_temp_free(val);                                         \
            dc->pc += 3 + id;                                           \
        }                                                               \
    } while (0)

#define MEMOP2_S(opmask, operation_fn)                          \
    do {                                                        \
        int op, mi, id, rs, rd, size;                           \
        TCGv mem, val;                                          \
        mi = (insn >> 22) & 3;                                  \
        op = (insn >> 18) & opmask;                             \
        id = (insn >> 16) & 3;                                  \
        rs = (insn >> 12) & 15;                                 \
        rd = (insn >> 8) & 15;                                  \
        size = (mi == 3) ? RX_MEMORY_WORD : mi;                 \
        mem = rx_index_addr(id, size, 3, rs, dc, env);          \
        val = tcg_temp_local_new();                             \
        if (mi != 3)                                            \
            rx_gen_ldst(size, RX_MEMORY_LD, val, mem);          \
        else                                                    \
            rx_gen_ldu(RX_MEMORY_WORD, val, mem);               \
        operation_fn(op, cpu_regs[rd], cpu_regs[rd], val);      \
        tcg_temp_free(mem);                                     \
        tcg_temp_free(val);                                     \
        dc->pc += 3 + id;                                       \
    } while (0)

#define MEMOP2_L(operation_fn)                                  \
    do {                                                        \
        int op, mi, id, rs, rd, size;                           \
        TCGv mem, val;                                          \
        mi = (insn >> 22) & 3;                                  \
        id = (insn >> 16) & 3;                                  \
        op = (insn >> 8) & 1;                                   \
        rs = (insn >> 4) & 15;                                  \
        rd = insn & 15;                                         \
        size = (mi == 3) ? RX_MEMORY_WORD : mi;                 \
        mem = rx_index_addr(id, size, 4, rs, dc, env);          \
        val = tcg_temp_local_new();                             \
        if (mi != 3)                                            \
            rx_gen_ldst(size, RX_MEMORY_LD, val, mem);          \
        else                                                    \
            rx_gen_ldu(RX_MEMORY_WORD, val, mem);               \
        operation_fn;                                           \
        tcg_temp_free(mem);                                     \
        tcg_temp_free(val);                                     \
        dc->pc += 4 + id;                                       \
    } while (0)

/* and rs, rd / or rs,rd / xor rs,rd / tst rs,rd */
/* and [rs].ub, rd / or [rs].ub,rd / xor [rs].ub,rd / tst [rs].ub,rd */
DEFINE_INSN(logic_op3)
{

    if ((insn & 0xff000000) == 0xfc000000) {
        /* xor / tst */
        MEMOP1_L(rx_gen_logic(3 - op, cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]),
                 rx_gen_logic(3 - op, cpu_regs[rd], cpu_regs[rd], mem));
    } else if ((insn & 0xf0000000) == 0x50000000) {
        /* and / or */
        MEMOP1_S(1, rx_gen_logic);
    } else
        g_assert_not_reached();
}

/* and [rs],rd / or [rs],rd / xor [rs],rd / tst [rs],rd */
DEFINE_INSN(logic_op4)
{
    if ((insn & 0x00300000) == 0x00200000) {
        /* xor / tst */
        MEMOP2_L(rx_gen_logic(3 - op, cpu_regs[rd], cpu_regs[rd], val));
    } else if ((insn & 0x00300000) == 0x00100000) {
        MEMOP2_S(3, rx_gen_logic);
    } else
        g_assert_not_reached();
}

#define OP3(opmask, operation_fn)                                       \
    do {                                                                \
        int op, rs, rs2, rd;                                            \
        op = (insn >> 20) & opmask;                                     \
        rd = (insn >> 16) & 15;                                         \
        rs = (insn >> 12) & 15;                                         \
        rs2 = (insn >> 8) & 15;                                         \
        operation_fn(op, cpu_regs[rd], cpu_regs[rs2], cpu_regs[rs]);    \
        dc->pc += 3;                                                    \
    } while (0)

/* and rs,rs2,rd / or rs,rs2,rd */
DEFINE_INSN(logic_op5)
{
    OP3(1, rx_gen_logic);
}

#define UPDATE_ALITH_CCOP(mode, arg1, arg2, ret)                   \
    do {                                                                \
        tcg_gen_mov_i32(ccop.op_a1[mode], arg1);                   \
        tcg_gen_mov_i32(ccop.op_a2[mode], arg2);                   \
        tcg_gen_mov_i32(ccop.op_r[mode], ret);                     \
        SET_MODE_CZSO(mode);                                   \
    } while (0)

#define UPDATE_ALITHIMM_CCOP(mode, arg1, arg2, ret)                 \
    do {                                                                \
        tcg_gen_mov_i32(ccop.op_a1[mode], arg1);                   \
        tcg_gen_movi_i32(ccop.op_a2[mode], arg2);                  \
        tcg_gen_mov_i32(ccop.op_r[mode], ret);                     \
        SET_MODE_CZSO(mode);                                   \
    } while (0)

static void rx_gen_sbb_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv invc;

    invc = tcg_temp_local_new();
    gen_helper_psw_c(invc, cpu_env);
    tcg_gen_xori_i32(invc, invc, 1);
    tcg_gen_sub_i32(ret, arg1, arg2);
    tcg_gen_sub_i32(ret, ret, invc);
    UPDATE_ALITH_CCOP(RX_PSW_OP_SUB, arg1, arg2, ret);
    tcg_temp_free(invc);
}

static void rx_gen_adc_i32(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv c;
    c = tcg_temp_local_new();
    gen_helper_psw_c(c, cpu_env);
    tcg_gen_add_i32(ret, arg1, arg2);
    tcg_gen_add_i32(ret, ret, c);
    UPDATE_ALITH_CCOP(RX_PSW_OP_ADD, arg1, arg2, ret);
    tcg_temp_free(c);
}

static void rx_gen_sbbi_i32(TCGv ret, TCGv arg1, int arg2)
{
    TCGv invc;

    invc = tcg_temp_local_new();
    gen_helper_psw_c(invc, cpu_env);
    tcg_gen_xori_i32(invc, invc, 1);
    tcg_gen_subi_i32(ret, arg1, arg2);
    tcg_gen_sub_i32(ret, ret, invc);
    UPDATE_ALITHIMM_CCOP(RX_PSW_OP_SUB, arg1, arg2, ret);
    tcg_temp_free(invc);
}

static void rx_gen_adci_i32(TCGv ret, TCGv arg1, int arg2)
{
    TCGv c;
    c = tcg_temp_local_new();
    gen_helper_psw_c(c, cpu_env);
    tcg_gen_addi_i32(ret, arg1, arg2);
    tcg_gen_add_i32(ret, ret, c);
    UPDATE_ALITHIMM_CCOP(RX_PSW_OP_ADD, arg1, arg2, ret);
    tcg_temp_free(c);
}

static void rx_alith_op(int opr, TCGv ret, TCGv r1, TCGv r2)
{
    static void (* const fn[])(TCGv ret, TCGv arg1, TCGv arg2) = {
        tcg_gen_sub_i32,
        tcg_gen_sub_i32,
        tcg_gen_add_i32,
        rx_gen_sbb_i32,
        rx_gen_adc_i32,
    };
    static const int opmodes[] = {RX_PSW_OP_SUB, RX_PSW_OP_SUB, RX_PSW_OP_ADD,
                                  RX_PSW_OP_SUB, RX_PSW_OP_ADD};
    int opmode = opmodes[opr];
    fn[opr](ccop.op_r[opmode], r1, r2);
    if (opr != RX_OP_CMP) {
        tcg_gen_mov_i32(ret, ccop.op_r[opmode]);
    }
    tcg_gen_mov_i32(ccop.op_a1[opmode], r1);
    tcg_gen_mov_i32(ccop.op_a2[opmode], r2);
    SET_MODE_CZSO(opmode);
}

static void rx_alith_imm_op(int opr, TCGv ret, TCGv r1, uint32_t imm)
{
    static void (* const fn[])(TCGv ret, TCGv arg1, int arg2) = {
        tcg_gen_subi_i32,
        tcg_gen_subi_i32,
        tcg_gen_addi_i32,
        rx_gen_sbbi_i32,
        rx_gen_adci_i32,
    };
    static const int opmodes[] = {RX_PSW_OP_SUB, RX_PSW_OP_SUB, RX_PSW_OP_ADD,
                                  RX_PSW_OP_SUB, RX_PSW_OP_ADD};
    int opmode = opmodes[opr];
    fn[opr](ccop.op_r[opmode], r1, imm);
    if (opr != RX_OP_CMP) {
        tcg_gen_mov_i32(ret, ccop.op_r[opmode]);
    }
    tcg_gen_mov_i32(ccop.op_a1[opmode], r1);
    tcg_gen_movi_i32(ccop.op_a2[opmode], imm);
    SET_MODE_CZSO(opmode);
}

DEFINE_INSN(addsub1)
{
    UIMM4OP(3, rx_alith_imm_op);
}

DEFINE_INSN(addsub2)
{
    MEMOP1_S(3, rx_alith_op);
}

DEFINE_INSN(addsub3)
{
    MEMOP2_S(3, rx_alith_op);
}

DEFINE_INSN(add4)
{
    /* Can't use UIMM4OP */
    int rs, rd, li;
    uint32_t imm;
    li = (insn >> 24) & 3;
    rs = (insn >> 20) & 15;
    rd = (insn >> 16) & 15;
    dc->pc = rx_load_simm(env, dc->pc + 2, li, &imm);
    rx_alith_imm_op(RX_OP_ADD, cpu_regs[rd], cpu_regs[rs], imm);
}

DEFINE_INSN(addsub5)
{
    OP3(3, rx_alith_op);
}

DEFINE_INSN(cmp2)
{
    int rd;
    uint32_t imm;

    rd = (insn >> 16) & 15;
    imm = (insn >> 8) & 0xff;

    rx_alith_imm_op(RX_OP_CMP, cpu_regs[rd], cpu_regs[rd], imm);
    dc->pc += 3;
}

DEFINE_INSN(cmp3)
{
    int li, rd;
    uint32_t imm;

    li = (insn >> 24) & 3;
    rd = (insn >> 16) & 15;

    dc->pc = rx_load_simm(env, dc->pc + 2, li, &imm);
    rx_alith_imm_op(RX_OP_CMP, cpu_regs[rd], cpu_regs[rd], imm);
}

DEFINE_INSN(cmp4)
{
    MEMOP1_S(3, rx_alith_op);
}

DEFINE_INSN(cmp5)
{
    MEMOP2_S(3, rx_alith_op);
}

DEFINE_INSN(adc1)
{
    SIMMOP_L(rx_alith_imm_op(op = RX_OP_ADC, cpu_regs[rd], cpu_regs[rd], imm));
}

DEFINE_INSN(adc2sbb1)
{
    int op, rs, rd;

    op = (insn >> 19) & 1;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;

    rx_alith_op(RX_OP_SBB + op, cpu_regs[rd], cpu_regs[rs], cpu_regs[rd]);
    dc->pc += 3;
}

DEFINE_INSN(adc3sbb2)
{
    int op, id, rs, rd;
    TCGv mem;
    TCGv val;

    val = tcg_temp_local_new();
    id = (insn >> 16) & 3;
    op = (insn >> 9) & 1;
    rs = (insn >> 4) & 15;
    rd = insn & 15;

    mem = rx_index_addr(id, RX_MEMORY_LONG, 4, rs, dc, env);
    rx_gen_ldst(RX_MEMORY_LONG, RX_MEMORY_LD, val, mem);

    rx_alith_op(RX_OP_SBB + op, cpu_regs[rd], val, cpu_regs[rd]);
    tcg_temp_free(mem);
    tcg_temp_free(val);
    dc->pc += 4 + id;
}

static void rx_gen_abs(TCGv ret, TCGv arg1)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_GE, arg1, 0, l1);
    tcg_gen_neg_i32(ret, arg1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(ret, arg1);
    gen_set_label(l2);
    tcg_gen_mov_i32(ccop.op_a1[RX_PSW_OP_ABS], arg1);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_ABS], ret);
    SET_MODE_ZSO(RX_PSW_OP_ABS);
}

static void rx_gen_neg(TCGv ret, TCGv arg1)
{
    tcg_gen_neg_i32(ret, arg1);
    tcg_gen_mov_i32(ccop.op_a1[RX_PSW_OP_ABS], arg1);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_ABS], ret);
    SET_MODE_ZSO(RX_PSW_OP_ABS);
}

static void rx_gen_not(TCGv ret, TCGv arg1)
{
    tcg_gen_not_i32(ret, arg1);
    tcg_gen_mov_i32(ccop.op_a1[RX_PSW_OP_LOGIC], arg1);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_LOGIC], ret);
    SET_MODE_ZS(RX_PSW_OP_LOGIC);
}

DEFINE_INSN(absnegnot1)
{
    static void (* const fn[])(TCGv ret, TCGv arg1) = {
        rx_gen_not,
        rx_gen_neg,
        rx_gen_abs,
    };
    int op, rd;
    op = (insn >> 20) & 3;
    rd = (insn >> 16) & 15;
    fn[op](cpu_regs[rd], cpu_regs[rd]);
    dc->pc += 2;
}

DEFINE_INSN(absnegnot2)
{
    static void (* const fn[])(TCGv ret, TCGv arg1) = {
        rx_gen_neg,
        rx_gen_not,
        rx_gen_abs,
    };
    int op, rs, rd;
    op = ((insn >> 18) & 3) - 1;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;
    if (op == -1) {
        rx_alith_op(RX_OP_SBB, cpu_regs[rd], cpu_regs[rs], cpu_regs[rd]);
    } else {
        fn[op](cpu_regs[rd], cpu_regs[rs]);
    }
    dc->pc += 3;
}

static void rx_mul_imm_op(int op, TCGv ret, TCGv arg1, int arg2)
{
    tcg_gen_muli_i32(ret, arg1, arg2);
}

static void rx_mul_op(int op, TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_mul_i32(ret, arg1, arg2);
}

DEFINE_INSN(mul1)
{
    UIMM4OP(3, rx_mul_imm_op);
}

DEFINE_INSN(mul2)
{
    SIMMOP_S(rx_mul_imm_op(op = RX_OP_MUL, cpu_regs[rd], cpu_regs[rd], imm));
}

DEFINE_INSN(mul3)
{
    MEMOP1_S(3, rx_mul_op);
}

DEFINE_INSN(mul4)
{
    MEMOP2_S(3, rx_mul_op);
}

DEFINE_INSN(mul5)
{
    OP3(3, rx_mul_op);
}

static void rx_div_imm_op(int op, TCGv ret, TCGv arg1, int arg2)
{
    static void (*fn[])(TCGv ret, TCGv arg1, TCGv arg2) = {
        tcg_gen_div_i32, tcg_gen_divu_i32,
    };
    TCGv _arg2 = tcg_const_local_i32(arg2);
    if (arg2) {
        fn[op](ret, arg1, _arg2);
        tcg_gen_mov_i32(ccop.op_a1[RX_PSW_OP_DIV], arg1);
        tcg_gen_movi_i32(ccop.op_a2[RX_PSW_OP_DIV], arg2);
        tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_DIV], ret);
        SET_MODE_O(RX_PSW_OP_DIV);
    }
    tcg_temp_free(_arg2);
}

static void rx_div_op(int op, TCGv ret, TCGv arg1, TCGv arg2)
{
    static void (*fn[])(TCGv ret, TCGv arg1, TCGv arg2) = {
        tcg_gen_div_i32, tcg_gen_divu_i32,
    };
    TCGLabel *l1 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, arg2, 0, l1);
    fn[op](ret, arg1, arg2);
    tcg_gen_mov_i32(ccop.op_a1[RX_PSW_OP_DIV], arg1);
    tcg_gen_mov_i32(ccop.op_a2[RX_PSW_OP_DIV], arg2);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_DIV], ret);
    SET_MODE_O(RX_PSW_OP_DIV);
    gen_set_label(l1);
}

DEFINE_INSN(div1)
{
    int divop = (insn >> 12) & 1;
    SIMMOP_L(rx_div_imm_op(op = divop, cpu_regs[rd], cpu_regs[rd], imm));
}

DEFINE_INSN(div2)
{
    MEMOP1_L(rx_div_op(op, cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]),
             rx_div_op(op, cpu_regs[rd], cpu_regs[rd], val));
}

DEFINE_INSN(div3)
{
    MEMOP2_L(rx_div_op(op, cpu_regs[rd], cpu_regs[rd], val));
}

static void rx_emul_imm_op(int op, TCGv rl, TCGv rh, TCGv arg1, int arg2)
{
    static void (*fn[])(TCGv rl, TCGv rh, TCGv arg1, TCGv arg2) = {
        tcg_gen_muls2_i32, tcg_gen_mulu2_i32,
    };
    TCGv _arg2 = tcg_const_local_i32(arg2);
    fn[op](rl, rh, arg1, _arg2);
    tcg_temp_free(_arg2);
}

static void rx_emul_op(int op, TCGv rl, TCGv rh, TCGv arg1, TCGv
                       arg2)
{
    static void (* const fn[])(TCGv rl, TCGv rh, TCGv arg1, TCGv arg2) = {
        tcg_gen_muls2_i32, tcg_gen_mulu2_i32,
    };
    fn[op](rl, rh, arg1, arg2);
}

DEFINE_INSN(emul1)
{
    SIMMOP_L(rx_emul_imm_op(op, cpu_regs[rd], cpu_regs[rd + 1],
                            cpu_regs[rd], imm));
}

DEFINE_INSN(emul2)
{
    MEMOP1_L(rx_emul_op(op, cpu_regs[rd], cpu_regs[rd + 1],
                        cpu_regs[rd], cpu_regs[rs]),
             rx_emul_op(op, cpu_regs[rd], cpu_regs[rd + 1],
                        cpu_regs[rd], val));
}

DEFINE_INSN(emul3)
{
    MEMOP2_L(rx_emul_op(op, cpu_regs[rd], cpu_regs[rd + 1],
                        cpu_regs[rd], val));
}

static void rx_minmax_imm_op(int op, TCGv ret, TCGv arg1, int arg2)
{
    static const TCGCond cond[] = {TCG_COND_GT, TCG_COND_LT};
    TCGv _arg2 = tcg_const_local_i32(arg2);
    tcg_gen_movcond_i32(cond[op], ret, arg1, _arg2, arg1, _arg2);
    tcg_temp_free(_arg2);
}

static void rx_minmax_op(int op, TCGv ret, TCGv arg1, TCGv arg2)
{
    static const TCGCond cond[] = {TCG_COND_GT, TCG_COND_LT};
    tcg_gen_movcond_i32(cond[op], ret, arg1, arg2, arg1, arg2);
}

DEFINE_INSN(minmax1)
{
    SIMMOP_L(rx_minmax_imm_op(op, cpu_regs[rd], cpu_regs[rd], imm));
}

DEFINE_INSN(minmax2)
{
    MEMOP1_L(rx_minmax_op(op, cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]),
             rx_minmax_op(op, cpu_regs[rd], cpu_regs[rd], mem));
}

DEFINE_INSN(minmax3)
{
    MEMOP2_L(rx_minmax_op(op, cpu_regs[rd], cpu_regs[rd], mem));
}

static void rx_shlri(TCGv ret, TCGv arg1, int arg2)
{
    if (arg2) {
        tcg_gen_shri_i32(ccop.op_r[RX_PSW_OP_SHLR], arg1, arg2 - 1);
        tcg_gen_andi_i32(ccop.op_a1[RX_PSW_OP_SHLR],
                         ccop.op_r[RX_PSW_OP_SHLR], 0x00000001);
        tcg_gen_shri_i32(ccop.op_r[RX_PSW_OP_SHLR],
                         ccop.op_r[RX_PSW_OP_SHLR], 1);
        tcg_gen_mov_i32(ret, ccop.op_r[RX_PSW_OP_SHLR]);
        SET_MODE_CZS(RX_PSW_OP_SHLR);
    }
}

static void rx_shari(TCGv ret, TCGv arg1, int arg2)
{
    if (arg2) {
        tcg_gen_sari_i32(ccop.op_r[RX_PSW_OP_SHAR], arg1, arg2 - 1);
        tcg_gen_andi_i32(ccop.op_a1[RX_PSW_OP_SHAR],
                         ccop.op_r[RX_PSW_OP_SHAR], 0x00000001);
        tcg_gen_sari_i32(ccop.op_r[RX_PSW_OP_SHAR],
                         ccop.op_r[RX_PSW_OP_SHAR], 1);
        tcg_gen_mov_i32(ret, ccop.op_r[RX_PSW_OP_SHAR]);
        SET_MODE_CZSO(RX_PSW_OP_SHAR);
    }
}

static void rx_shlli(TCGv ret, TCGv arg1, int arg2)
{
    if (arg2) {
        tcg_gen_shri_i32(ccop.op_a1[RX_PSW_OP_SHLL], arg1, 32 - arg2);
        tcg_gen_mov_i32(ccop.op_a2[RX_PSW_OP_SHLL], arg1);
        tcg_gen_shli_i32(ret, arg1, arg2);
        tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_SHLL], ret);
        SET_MODE_CZSO(RX_PSW_OP_SHLL);
    }
}

static void rx_shlr(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    TCGLabel *l1;
    t0 = tcg_temp_local_new();
    l1 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, arg2, 0, l1);
    tcg_gen_subi_i32(t0, arg2, 1);
    tcg_gen_shr_i32(ccop.op_r[RX_PSW_OP_SHLR], arg1, t0);
    tcg_gen_andi_i32(ccop.op_a1[RX_PSW_OP_SHLR],
                     ccop.op_r[RX_PSW_OP_SHLR], 0x00000001);
    tcg_gen_shri_i32(ccop.op_r[RX_PSW_OP_SHLR],
                     ccop.op_r[RX_PSW_OP_SHLR], 1);
    tcg_gen_mov_i32(ret, ccop.op_r[RX_PSW_OP_SHLR]);
    gen_set_label(l1);
    tcg_temp_free(t0);
}

static void rx_shar(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    TCGLabel *l1;
    t0 = tcg_temp_local_new();
    l1 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, arg2, 0, l1);
    tcg_gen_subi_i32(t0, arg2, 1);
    tcg_gen_sar_i32(ccop.op_r[RX_PSW_OP_SHAR], arg1, t0);
    tcg_gen_andi_i32(ccop.op_a1[RX_PSW_OP_SHAR],
                     ccop.op_r[RX_PSW_OP_SHAR], 0x00000001);
    tcg_gen_sari_i32(ccop.op_r[RX_PSW_OP_SHAR],
                     ccop.op_r[RX_PSW_OP_SHAR], 1);
    tcg_gen_mov_i32(ret, ccop.op_r[RX_PSW_OP_SHAR]);
    gen_set_label(l1);
    tcg_temp_free(t0);
}

static void rx_shll(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t0;
    TCGLabel *l1;
    t0 = tcg_temp_local_new();
    l1 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, arg2, 0, l1);
    tcg_gen_movi_i32(t0, 32);
    tcg_gen_sub_i32(t0, t0, arg2);
    tcg_gen_shr_i32(ccop.op_a1[RX_PSW_OP_SHLL], arg1, t0);
    tcg_gen_mov_i32(ccop.op_a2[RX_PSW_OP_SHLL], arg1);
    tcg_gen_shl_i32(ret, arg1, arg2);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_SHLL], ret);
    SET_MODE_CZSO(RX_PSW_OP_SHLL);
    gen_set_label(l1);
    tcg_temp_free(t0);
}

DEFINE_INSN(shift1)
{
    static void (* const fn[])(TCGv ret, TCGv arg1, int arg2) = {
        rx_shlri, rx_shari, rx_shlli,
    };
    int op, imm, rd;
    op = (insn >> 25) & 7;
    imm = (insn >> 20) & 0x1f;
    rd = (insn >> 16) & 15;
    if (imm != 0) {
        fn[op - 4](cpu_regs[rd], cpu_regs[rd], imm);
    }
    dc->pc += 2;
}

DEFINE_INSN(shift2)
{
    static void (* const fn[])(TCGv ret, TCGv arg1, TCGv arg2) = {
        rx_shlr, rx_shar, rx_shll,
    };
    int op, rs, rd;
    op = (insn >> 16) & 3;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;
    fn[op](cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]);
    dc->pc += 3;
}

DEFINE_INSN(shift3)
{
    static void (* const fn[])(TCGv ret, TCGv arg1, int arg2) = {
        rx_shlri, rx_shari, rx_shlli,
    };
    int op, imm, rs, rd;
    op = (insn >> 21) & 3;
    imm = (insn >> 16) & 0x1f;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;
    if (imm != 0) {
        fn[op](cpu_regs[rd], cpu_regs[rs], imm);
    }
    dc->pc += 3;
}

DEFINE_INSN(roc)
{
    int dir, rd;
    TCGv cin;

    dir = (insn >> 20) & 1;
    rd = (insn >> 16) & 15;
    cin = tcg_temp_local_new();
    gen_helper_psw_c(cin, cpu_env);
    if (dir) {
        tcg_gen_shri_i32(ccop.op_a1[RX_PSW_OP_SHLR], cpu_regs[rd], 31);
        tcg_gen_shli_i32(cpu_regs[rd], cpu_regs[rd], 1);
        tcg_gen_or_i32(cpu_regs[rd], cpu_regs[rd], cin);
    } else {
        tcg_gen_andi_i32(ccop.op_a1[RX_PSW_OP_SHLR], cpu_regs[rd], 0x00000001);
        tcg_gen_shri_i32(cpu_regs[rd], cpu_regs[rd], 1);
        tcg_gen_shli_i32(cin, cin, 31);
        tcg_gen_or_i32(cpu_regs[rd], cpu_regs[rd], cin);
    }
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_SHLR], cpu_regs[rd]);
    SET_MODE_CZS(RX_PSW_OP_SHLR);
    tcg_temp_free(cin);
    dc->pc += 2;
}

DEFINE_INSN(rot1)
{
    int dir, imm, rd;
    dir = (insn >> 17) & 1;
    imm = (insn >> 12) & 31;
    rd = (insn >> 8) & 15;
    tcg_gen_movi_i32(ccop.op_a1[RX_PSW_OP_ROT], dir);
    if (dir) {
        tcg_gen_rotli_i32(cpu_regs[rd], cpu_regs[rd], imm);
    } else {
        tcg_gen_rotri_i32(cpu_regs[rd], cpu_regs[rd], imm);
    }
    tcg_gen_andi_i32(ccop.op_r[RX_PSW_OP_ROT], cpu_regs[rd], 0x00000001);
    SET_MODE_CZS(RX_PSW_OP_ROT);
    dc->pc += 3;
}

DEFINE_INSN(rot2)
{
    int dir, rs, rd;
    dir = (insn >> 17) & 1;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;
    tcg_gen_movi_i32(ccop.op_a1[RX_PSW_OP_ROT], dir);
    if (dir) {
        tcg_gen_rotl_i32(cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]);
    } else {
        tcg_gen_rotr_i32(cpu_regs[rd], cpu_regs[rd], cpu_regs[rs]);
    }
    tcg_gen_andi_i32(ccop.op_r[RX_PSW_OP_ROT], cpu_regs[rd], 0x00000001);
    SET_MODE_CZS(RX_PSW_OP_ROT);
    dc->pc += 3;
}

DEFINE_INSN(sat)
{
    int rd;
    TCGv s, o, plus, minus, one;
    TCGLabel *l1;

    l1 = gen_new_label();
    rd = (insn >> 16) & 15;
    s = tcg_temp_local_new();
    o = tcg_temp_local_new();
    plus = tcg_const_local_i32(0x7fffffff);
    minus = tcg_const_local_i32(0x80000000);
    one = tcg_const_local_i32(1);
    gen_helper_psw_s(s, cpu_env);
    gen_helper_psw_o(o, cpu_env);
    tcg_gen_brcondi_i32(TCG_COND_NE, o, 1, l1);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_regs[rd], s, one, plus, minus);
    gen_set_label(l1);
    tcg_temp_free(s);
    tcg_temp_free(o);
    tcg_temp_free(plus);
    tcg_temp_free(minus);
    tcg_temp_free(one);
    dc->pc += 2;
}

DEFINE_INSN(satr)
{
    TCGv s, o;
    TCGLabel *l1, *l2;
    l1 = gen_new_label();
    l2 = gen_new_label();

    s = tcg_temp_local_new();
    o = tcg_temp_local_new();
    gen_helper_psw_s(s, cpu_env);
    gen_helper_psw_o(o, cpu_env);
    tcg_gen_brcondi_i32(TCG_COND_NE, o, 1, l2);
    tcg_gen_brcondi_i32(TCG_COND_EQ, s, 1, l1);
    tcg_gen_movi_i32(cpu_regs[6], 0x7fffffff);
    tcg_gen_movi_i32(cpu_regs[5], 0xffffffff);
    tcg_gen_movi_i32(cpu_regs[4], 0xffffffff);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(cpu_regs[6], 0x80000000);
    tcg_gen_movi_i32(cpu_regs[5], 0x00000000);
    tcg_gen_movi_i32(cpu_regs[4], 0x00000000);
    gen_set_label(l2);
    tcg_temp_free(s);
    tcg_temp_free(o);
    dc->pc += 2;
}

DEFINE_INSN(rmpa)
{
    int sz;
    TCGLabel *l0, *l1, *l2, *l3;
    TCGv t0, t1, t2, t3;

    sz = (insn >> 16) & 3;
    l0 = gen_new_label();
    l1 = gen_new_label();
    l2 = gen_new_label();
    l3 = gen_new_label();
    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();
    t2 = tcg_temp_local_new();
    t3 = tcg_temp_local_new();
    gen_set_label(l0);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[3], 0, l2);
    rx_gen_ldst(sz, RX_MEMORY_LD, t0, cpu_regs[1]);
    tcg_gen_addi_i32(cpu_regs[1], cpu_regs[1], 1 << sz);
    tcg_gen_addi_i32(cpu_regs[2], cpu_regs[2], 1 << sz);
    rx_gen_ldst(sz, RX_MEMORY_LD, t1, cpu_regs[2]);
    tcg_gen_muls2_i32(t2, t3, t0, t1);
    tcg_gen_add2_i32(t0, t1, cpu_regs[4], cpu_regs[5], t2, t3);
    tcg_gen_brcond_i32(TCG_COND_GT, t1, cpu_regs[5], l1);
    tcg_gen_brcond_i32(TCG_COND_GT, t0, cpu_regs[4], l1);
    tcg_gen_addi_i32(cpu_regs[6], cpu_regs[6], 1);
    gen_set_label(l1);
    tcg_gen_subi_i32(cpu_regs[3], cpu_regs[3], 1);
    tcg_gen_br(l0);
    gen_set_label(l2);
    tcg_gen_ext16s_i32(cpu_regs[6], cpu_regs[6]);
    tcg_gen_shri_i32(cpu_psw_s, cpu_regs[6], 31);
    tcg_gen_movi_i32(cpu_psw_o, 0);
    tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, 0x00ff);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[6], 0, l3);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[6], -1, l3);
    tcg_gen_movi_i32(cpu_psw_o, 1);
    gen_set_label(l3);
    tcg_temp_free(t3);
    tcg_temp_free(t2);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    dc->pc += 2;
}

static void bsetmem(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_local_new();
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, val, mem);
    tcg_gen_or_i32(val, val, mask);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, val, mem);
    tcg_temp_free(val);
}

static void bclrmem(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_local_new();
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, val, mem);
    tcg_gen_not_i32(mask, mask);
    tcg_gen_and_i32(val, val, mask);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, val, mem);
    tcg_temp_free(val);
}

static void btstmem(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_local_new();
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, val, mem);
    tcg_gen_and_i32(val, val, mask);
    tcg_gen_setcondi_i32(TCG_COND_NE, ccop.op_r[RX_PSW_OP_BTST], val, 0);
    SET_MODE_CZ(RX_PSW_OP_BTST);
    tcg_temp_free(val);
}

static void bnotmem(TCGv mem, TCGv mask)
{
    TCGv val;
    val = tcg_temp_local_new();
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, val, mem);
    tcg_gen_xor_i32(val, val, mask);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, val, mem);
    tcg_temp_free(val);
}

static void bsetreg(TCGv reg, TCGv mask)
{
    tcg_gen_or_i32(reg, reg, mask);
}

static void bclrreg(TCGv reg, TCGv mask)
{
    tcg_gen_not_i32(mask, mask);
    tcg_gen_and_i32(reg, reg, mask);
}

static void btstreg(TCGv reg, TCGv mask)
{
    TCGv t0;
    t0 = tcg_temp_local_new();
    tcg_gen_and_i32(t0, reg, mask);
    tcg_gen_setcondi_i32(TCG_COND_NE, ccop.op_r[RX_PSW_OP_BTST], t0, 0);
    SET_MODE_CZ(RX_PSW_OP_BTST);
    tcg_temp_free(t0);
}

static void bnotreg(TCGv reg, TCGv mask)
{
    tcg_gen_xor_i32(reg, reg, mask);
}

DEFINE_INSN(bop1)
{
    static void (* const fn[])(TCGv mem, TCGv mask) = {
        bsetmem, bclrmem, btstmem,
    };
    int op, id, rd, imm;
    TCGv mem, mask;
    op = ((insn >> 25) & 6) | ((insn >> 19) & 1);
    id = (insn >> 24) & 3;
    rd = (insn >> 20) & 15;
    imm = (insn >> 16) & 7;
    mem = rx_index_addr(id, RX_MEMORY_BYTE, 2, rd, dc, env);
    mask = tcg_const_local_i32(1 << imm);
    fn[op](mem, mask);
    tcg_temp_free(mem);
    tcg_temp_free(mask);
    dc->pc += 2 + id;
}

DEFINE_INSN(bop2)
{
    static void (*bmem[])(TCGv mem, TCGv mask) = {
        bsetmem, bclrmem, btstmem, bnotmem,
    };
    static void (*breg[])(TCGv reg, TCGv mask) = {
        bsetreg, bclrreg, btstreg, bnotreg,
    };
    int op, id, rd, rs;
    TCGv mem, mask;
    op = (insn >> 18) & 3;
    id = (insn >> 16) & 3;
    rd = (insn >> 12) & 15;
    rs = (insn >> 8) & 15;

    mask = tcg_temp_local_new();
    tcg_gen_movi_i32(mask, 1);
    tcg_gen_shl_i32(mask, mask, cpu_regs[rs]);
    if (id < 3) {
        mem = rx_index_addr(id, RX_MEMORY_BYTE, 2, rd, dc, env);
        bmem[op](mem, mask);
        tcg_temp_free(mem);
        dc->pc += 3 + id;
    } else {
        breg[op](cpu_regs[rd], mask);
        dc->pc += 3;
    }
    tcg_temp_free(mask);
}

DEFINE_INSN(bop3)
{
    static void (*fn[])(TCGv reg, TCGv mask) = {
        bsetreg, bclrreg, btstreg,
    };
    int op, imm, rd;
    TCGv mask;
    op = (insn >> 25) & 3;
    imm = (insn >> 20) & 31;
    rd = (insn >> 16) & 15;
    mask = tcg_const_local_i32(1 << imm);
    fn[op](cpu_regs[rd], mask);
    tcg_temp_free(mask);
    dc->pc += 2;
}

DEFINE_INSN(bnot1)
{
    int imm, id, rd;
    TCGv mem, val;
    imm = (insn >> 18) & 7;
    id = (insn >> 16) & 3;
    rd = (insn >> 12) & 15;
    mem = rx_index_addr(id, RX_MEMORY_BYTE, 2, rd, dc, env);
    val = tcg_temp_local_new();
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, val, mem);
    tcg_gen_xori_i32(val, val, 1 << imm);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, val, mem);
    dc->pc += 3;
}

DEFINE_INSN(bmcnd1)
{
    int imm, id, rd;
    TCGv mem, val, cd, result;
    TCGLabel *l1, *l2;
    l1 = gen_new_label();
    l2 = gen_new_label();
    imm = (insn >> 18) & 7;
    id = (insn >> 16) & 3;
    rd = (insn >> 12) & 15;
    cd = tcg_const_local_i32((insn >> 8) & 15);
    val = tcg_temp_local_new();
    result = tcg_temp_local_new();
    mem = rx_index_addr(id, RX_MEMORY_BYTE, 2, rd, dc, env);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, val, mem);
    if (((insn >> 8) & 15) == 15) {
        /* special case bnot #imm, mem */
        tcg_gen_xori_i32(val, val, 1 << imm);
    } else {
        gen_helper_cond(result, cpu_env, cd);
        tcg_gen_brcondi_i32(TCG_COND_NE, result, 0, l1);
        tcg_gen_andi_i32(val, val, ~(1 << imm));
        tcg_gen_br(l2);
        gen_set_label(l1);
        tcg_gen_ori_i32(val, val, 1 << imm);
        gen_set_label(l2);
    }
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, val, mem);
    tcg_temp_free(mem);
    tcg_temp_free(val);
    tcg_temp_free(cd);
    tcg_temp_free(result);
    dc->pc += 3 + id;
}

DEFINE_INSN(bmcnd2)
{
    int imm, rd;
    TCGv cd, result;
    TCGLabel *l1, *l2;
    l1 = gen_new_label();
    l2 = gen_new_label();
    imm = (insn >> 16) & 31;
    cd = tcg_const_local_i32((insn >> 12) & 15);
    rd = (insn >> 8) & 15;
    if (((insn >> 12) & 15) == 15) {
        /* special case bnot #imm, reg */
        tcg_gen_xori_i32(cpu_regs[rd], cpu_regs[rd], 1 << imm);
    } else {
        result = tcg_temp_local_new();
        gen_helper_cond(result, cpu_env, cd);
        tcg_gen_brcondi_i32(TCG_COND_NE, result, 0, l1);
        tcg_gen_andi_i32(cpu_regs[rd], cpu_regs[rd], ~(1 << imm));
        tcg_gen_br(l2);
        gen_set_label(l1);
        tcg_gen_ori_i32(cpu_regs[rd], cpu_regs[rd], 1 << imm);
        gen_set_label(l2);
        tcg_temp_free(result);
    }
    tcg_temp_free(cd);
    dc->pc += 3;
}

DEFINE_INSN(scmpu)
{
    TCGLabel *l1, *l2;
    TCGv t0, t1;
    l1 = gen_new_label();
    l2 = gen_new_label();
    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[3], 0, l2);
    gen_set_label(l1);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, t1, cpu_regs[2]);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, t0, cpu_regs[1]);
    tcg_gen_addi_i32(cpu_regs[1], cpu_regs[1], 1);
    tcg_gen_addi_i32(cpu_regs[2], cpu_regs[2], 1);
    tcg_gen_subi_i32(cpu_regs[3], cpu_regs[3], 1);
    tcg_gen_brcond_i32(TCG_COND_NE, t0, t1, l2);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l2);
    tcg_gen_brcondi_i32(TCG_COND_GTU, cpu_regs[3], 0, l1);
    gen_set_label(l2);
    tcg_gen_sub_i32(ccop.op_r[RX_PSW_OP_STRING], t0, t1);
    SET_MODE_CZ(RX_PSW_OP_STRING);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    dc->pc += 2;
}

DEFINE_INSN(smovbfu)
{
    TCGLabel *l1, *l2;
    TCGv t0;
    int dir, term;
    l1 = gen_new_label();
    l2 = gen_new_label();
    t0 = tcg_temp_local_new();
    term = (insn >> 19) & 1;
    dir = (insn >> 18) & 1;
    gen_set_label(l1);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[3], 0, l2);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_LD, t0, cpu_regs[2]);
    rx_gen_ldst(RX_MEMORY_BYTE, RX_MEMORY_ST, t0, cpu_regs[1]);
    if (dir) {
        tcg_gen_addi_i32(cpu_regs[1], cpu_regs[1], 1);
        tcg_gen_addi_i32(cpu_regs[2], cpu_regs[2], 1);
    } else {
        tcg_gen_subi_i32(cpu_regs[1], cpu_regs[1], 1);
        tcg_gen_subi_i32(cpu_regs[2], cpu_regs[2], 1);
    }
    tcg_gen_subi_i32(cpu_regs[3], cpu_regs[3], 1);
    if (term == 0) {
        tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l2);
    }
    tcg_gen_br(l1);
    gen_set_label(l2);
    tcg_temp_free(t0);
    dc->pc += 2;
}

DEFINE_INSN(sstr)
{
    int size;
    TCGLabel *l1, *l2;
    l1 = gen_new_label();
    l2 = gen_new_label();

    size = (insn >> 16) & 3;
    gen_set_label(l1);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[3], 0, l2);
    rx_gen_ldst(size, RX_MEMORY_ST, cpu_regs[2], cpu_regs[1]);
    tcg_gen_addi_i32(cpu_regs[1], cpu_regs[1], 1 << size);
    tcg_gen_subi_i32(cpu_regs[3], cpu_regs[3], 1);
    tcg_gen_br(l1);
    gen_set_label(l2);
    dc->pc += 2;
}

DEFINE_INSN(ssearch)
{
    int match, size;
    TCGv t0;
    TCGLabel *l1, *l2;
    l1 = gen_new_label();
    l2 = gen_new_label();
    t0 = tcg_temp_local_new();
    match = (insn >> 18) & 1;
    size = (insn >> 16) & 3;
    gen_set_label(l1);
    rx_gen_ldu(size, t0, cpu_regs[1]);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_regs[3], 0, l2);
    tcg_gen_addi_i32(cpu_regs[1], cpu_regs[1], 1 << size);
    tcg_gen_subi_i32(cpu_regs[3], cpu_regs[3], 1);
    tcg_gen_brcond_i32(match ? TCG_COND_EQ : TCG_COND_NE,
                       t0, cpu_regs[2], l2);
    tcg_gen_br(l1);
    gen_set_label(l2);
    tcg_gen_sub_i32(ccop.op_r[RX_PSW_OP_STRING], t0, cpu_regs[2]);
    SET_MODE_CZ(RX_PSW_OP_STRING);
    tcg_temp_free(t0);
    dc->pc += 2;
}

static void bra_main(int dst, DisasContext *dc)
{
    tcg_gen_movi_i32(cpu_pc, dc->pc += dst);
    dc->base.is_jmp = DISAS_JUMP;
}

DEFINE_INSN(bra1)
{
    unsigned int dst;
    dst = (insn >> 24) & 7;
    if (dst < 3) {
        dst += 8;
    }
    bra_main(dst, dc);
    dc->pc += 1;
}

DEFINE_INSN(bra2)
{
    char dst;
    dst = (insn >> 16) & 255;
    bra_main(dst, dc);
    dc->pc += 2;
}

DEFINE_INSN(bra3)
{
    short dst;
    dst = (insn & 0xff00) | ((insn >> 16) & 0xff);
    bra_main(dst, dc);
    dc->pc += 3;
}

DEFINE_INSN(bra4)
{
    unsigned short dstl;
    char dsth;
    dstl = (insn & 0xff00) | ((insn >> 16) & 0xff);
    dsth = insn & 255;
    bra_main((dsth << 16) | dstl, dc);
    dc->pc += 4;
}

DEFINE_INSN(bra5)
{
    int rd;
    rd = (insn >> 16) & 15;
    tcg_gen_addi_i32(cpu_pc, cpu_regs[rd], dc->pc);
    dc->base.is_jmp = DISAS_JUMP;
}

static void bcnd_main(int cd, int dst, int len, DisasContext *dc)
{
    TCGv zero, cond, result, t, f;
    t = tcg_const_local_i32(dc->pc + dst);
    f = tcg_const_local_i32(dc->pc + len);
    result = tcg_temp_local_new();
    cond = tcg_const_local_i32(cd);
    zero = tcg_const_local_i32(0);
    gen_helper_cond(result, cpu_env, cond);

    tcg_gen_movcond_i32(TCG_COND_NE, cpu_pc,
                        result, zero, t, f);
    dc->base.is_jmp = DISAS_JUMP;
    tcg_temp_free(t);
    tcg_temp_free(f);
    tcg_temp_free(zero);
    tcg_temp_free(cond);
    tcg_temp_free(result);
    dc->pc += len;
}

DEFINE_INSN(bcnd1)
{
    int cd, dst;
    cd = (insn >> 27) & 1;
    dst = (insn >> 24) & 7;
    if (dst < 3) {
        dst += 8;
    }
    bcnd_main(cd, dst, 1, dc);
}

DEFINE_INSN(bcnd2)
{
    int cd;
    char dst;
    cd = (insn >> 24) & 15;
    dst = (insn >> 16) & 255;
    bcnd_main(cd, dst, 2, dc);
}

DEFINE_INSN(bcnd3)
{
    int cd;
    short dst;
    cd = (insn >> 24) & 1;
    dst = (insn & 0xff00) | ((insn >> 16) & 0xff);
    bcnd_main(cd, dst, 3, dc);
}

static void pc_save_stack(int len, DisasContext *dc)
{
    TCGv save_pc;
    save_pc = tcg_const_local_i32(dc->pc + len);
    tcg_gen_subi_i32(cpu_regs[0], cpu_regs[0], 4);
    tcg_gen_qemu_st32(save_pc, cpu_regs[0], 0);
    tcg_temp_free(save_pc);
}

DEFINE_INSN(bsr1)
{
    short dst;
    pc_save_stack(3, dc);
    dst = (insn & 0xff00) | ((insn >> 16) & 0xff);
    bra_main(dst, dc);
}

DEFINE_INSN(bsr2)
{
    unsigned short dstl;
    char dsth;
    pc_save_stack(4, dc);
    dstl = (insn & 0xff00) | ((insn >> 16) & 0xff);
    dsth = insn & 255;
    bra_main((dsth << 16) | dstl, dc);
}

DEFINE_INSN(bsr3)
{
    int rd;
    rd = (insn >> 16) & 15;
    pc_save_stack(2, dc);
    tcg_gen_addi_i32(cpu_pc, cpu_regs[rd], dc->pc);
    dc->base.is_jmp = DISAS_JUMP;
}

DEFINE_INSN(jmpjsr)
{
    int is_jsr, rd;
    is_jsr = (insn >> 20) & 1;
    rd = (insn >> 16) & 15;
    if (is_jsr) {
        pc_save_stack(2, dc);
    }
    tcg_gen_mov_i32(cpu_pc, cpu_regs[rd]);
    dc->base.is_jmp = DISAS_JUMP;
    dc->pc += 2;
}

DEFINE_INSN(rts)
{
    tcg_gen_qemu_ld32u(cpu_pc, cpu_regs[0], 0);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    dc->base.is_jmp = DISAS_JUMP;
    dc->pc += 1;
}

DEFINE_INSN(rtsd1)
{
    int src;
    src = (insn >> 16) & 255;
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], src << 2);
    tcg_gen_qemu_ld32u(cpu_pc, cpu_regs[0], 0);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    dc->base.is_jmp = DISAS_JUMP;
    dc->pc += 2;
}

DEFINE_INSN(rtsd2)
{
    int src, dst, dst2;
    dst = (insn >> 20) & 15;
    dst2 = (insn >> 16) & 15;
    src = (insn >> 8) & 255;
    src -= (dst2 - dst + 1);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], src << 2);
    for (; dst <= dst2; dst++) {
        tcg_gen_qemu_ld32u(cpu_regs[dst], cpu_regs[0], 0);
        tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    }
    tcg_gen_qemu_ld32u(cpu_pc, cpu_regs[0], 0);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    dc->base.is_jmp = DISAS_JUMP;
    dc->pc += 3;
}

DEFINE_INSN(rxbrk)
{
    tcg_gen_movi_i32(cpu_pc, dc->pc + 1);
    gen_helper_rxbrk(cpu_env);
    dc->base.is_jmp = DISAS_NORETURN;
    dc->pc += 1;
}

DEFINE_INSN(rxint)
{
    int imm;
    TCGv vec;
    imm = (insn >> 8) & 0xff;
    vec = tcg_const_local_i32(imm);
    tcg_gen_movi_i32(cpu_pc, dc->pc + 3);
    gen_helper_rxint(cpu_env, vec);
    tcg_temp_free(vec);
    dc->base.is_jmp = DISAS_NORETURN;
    dc->pc += 3;
}

DEFINE_INSN(clrsetpsw)
{
    TCGv psw[] = {
        cpu_psw_c, cpu_psw_z, cpu_psw_s, cpu_psw_o,
        NULL, NULL, NULL, NULL,
        cpu_psw_i, cpu_psw_u, NULL, NULL,
        NULL, NULL, NULL, NULL
    };
    static const uint32_t opmask[] = {~0x000f, ~0x00f0, ~0x0f00, ~0xf000};
    int mode, dst;
    TCGLabel *l;

    mode = (insn >> 20 & 1);
    dst = (insn >> 16) & 15;
    l = gen_new_label();
    if (dst >= 8) {
        tcg_gen_brcondi_i32(TCG_COND_NE, cpu_psw_pm, 0, l);
    }
    tcg_gen_movi_i32(psw[dst], (mode ? 0 : 1));
    gen_set_label(l);
    if (dst < 4) {
        tcg_gen_andi_i32(ccop.op_mode, ccop.op_mode, opmask[dst]);
    }
    dc->pc += 2;
}

DEFINE_INSN(mvfc)
{
    int rd, cr;
    TCGv _cr;
    cr = (insn >> 12) & 15;
    _cr = tcg_const_i32(cr);
    rd = (insn >> 8) & 15;
    if (cr == 1) {
        tcg_gen_movi_i32(cpu_regs[rd], dc->pc);
    } else {
        gen_helper_mvfc(cpu_regs[rd], cpu_env, _cr);
    }
    tcg_temp_free(_cr);
    dc->pc += 3;
}

DEFINE_INSN(mvtc1)
{
    int li;
    uint32_t imm;
    TCGv cr, _imm;

    li = (insn >> 18) & 3;
    cr = tcg_const_i32((insn >> 8) & 15);
    dc->pc = rx_load_simm(env, dc->pc + 3, li, &imm);
    _imm = tcg_const_i32(imm);
    gen_helper_mvtc(cpu_env, cr, _imm);
    tcg_temp_free(cr);
    tcg_temp_free(_imm);
}

DEFINE_INSN(mvtc2)
{
    int rs;
    TCGv cr;
    rs = (insn >> 12) & 15;
    cr = tcg_const_i32((insn >> 8) & 15);
    gen_helper_mvtc(cpu_env, cr, cpu_regs[rs]);
    dc->pc += 3;
    tcg_temp_free(cr);
}

static void check_previleged(void)
{
    TCGLabel *good;
    good = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_psw_pm, 0, good);
    gen_helper_raise_privilege_violation(cpu_env);
    gen_set_label(good);
}

DEFINE_INSN(mvtipl)
{
    int ipl;
    check_previleged();
    ipl = (insn >> 8) & 15;
    tcg_gen_movi_i32(cpu_psw_ipl, ipl);
    dc->pc += 3;
}

DEFINE_INSN(rte)
{
    check_previleged();
    tcg_gen_qemu_ld32u(cpu_pc, cpu_regs[0], 0);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    tcg_gen_qemu_ld32u(cpu_psw, cpu_regs[0], 0);
    tcg_gen_addi_i32(cpu_regs[0], cpu_regs[0], 4);
    gen_helper_unpack_psw(cpu_env);
    dc->base.is_jmp = DISAS_JUMP;
    dc->pc += 2;
}

DEFINE_INSN(rtfi)
{
    check_previleged();
    tcg_gen_mov_i32(cpu_pc, cpu_bpc);
    tcg_gen_mov_i32(cpu_psw, cpu_bpsw);
    gen_helper_unpack_psw(cpu_env);
    dc->base.is_jmp = DISAS_JUMP;
    dc->pc += 2;
}

DEFINE_INSN(rxwait)
{
    check_previleged();
    tcg_gen_addi_i32(cpu_pc, cpu_pc, 2);
    gen_helper_wait(cpu_env);
    dc->pc += 2;
}

DEFINE_INSN(fimm)
{
    int op, rd, fop;
    uint32_t imm;
    TCGv _op, t0;

    op = (insn >> 12) & 7;
    rd = (insn >> 8) & 15;
    dc->pc = rx_load_simm(env, dc->pc + 4, 3, &imm);
    t0 = tcg_const_i32(imm);
    _op = tcg_const_i32(op);
    fop = (op != 1) ? RX_PSW_OP_FLOAT : RX_PSW_OP_FCMP;
    gen_helper_floatop(ccop.op_r[fop], cpu_env, _op, cpu_regs[rd], t0);
    if (op != 1) {
        tcg_gen_mov_i32(cpu_regs[rd], ccop.op_r[RX_PSW_OP_FLOAT]);
        SET_MODE_ZS(RX_PSW_OP_FLOAT);
    } else
        SET_MODE_ZSO(RX_PSW_OP_FCMP);
    tcg_temp_free(t0);
    tcg_temp_free(_op);
}

DEFINE_INSN(fmem)
{
    int op, id, rs, rd, fop;
    TCGv _op, t1;

    op = (insn >> 18) & 7;
    id = (insn >> 16) & 3;
    rs = (insn >> 8) & 15;
    rd = (insn >> 8) & 15;

    t1 = tcg_temp_local_new();
    if (id < 3) {
        TCGv t0;
        t0 = rx_index_addr(id, 2, 3, rs, dc, env);
        tcg_gen_qemu_ld32u(t1, t0, 0);
        dc->pc += 3 + id;
        tcg_temp_free(t0);
    } else {
        tcg_gen_mov_i32(t1, cpu_regs[rs]);
        dc->pc += 3;
    }
    switch (op) {
    case 0 ... 4:
        _op = tcg_const_i32(op);
        fop = (op != 1) ? RX_PSW_OP_FLOAT : RX_PSW_OP_FCMP;
        gen_helper_floatop(ccop.op_r[fop], cpu_env,
                           _op, cpu_regs[rd], t1);
        if (op != 1) {
            tcg_gen_mov_i32(cpu_regs[rd], ccop.op_r[RX_PSW_OP_FLOAT]);
            SET_MODE_ZS(RX_PSW_OP_FLOAT);
        } else
            SET_MODE_ZSO(RX_PSW_OP_FCMP);
        tcg_temp_free(_op);
        break;
    case 5:
        gen_helper_ftoi(cpu_regs[rd], cpu_env, t1);
        tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_FLOAT], cpu_regs[rd]);
        SET_MODE_ZS(RX_PSW_OP_FLOAT);
        break;
    case 6:
        gen_helper_round(cpu_regs[rd], cpu_env, t1);
        tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_FLOAT], cpu_regs[rd]);
        SET_MODE_ZS(RX_PSW_OP_FLOAT);
        break;
    }
    tcg_temp_free(t1);
}

DEFINE_INSN(itof1)
{
    int id, rs, rd;
    TCGv mem, t0;

    id = (insn >> 16) & 3;
    rs = (insn >> 12) & 15;
    rd = (insn >> 8) & 15;
    t0 = tcg_temp_local_new();
    if (id < 3) {
        mem = rx_index_addr(id, 2, 3, rs, dc, env);
        rx_gen_ldu(RX_MEMORY_BYTE, t0, mem);
        tcg_temp_free(mem);
        dc->pc += 3 + id;
    } else {
        tcg_gen_mov_i32(t0, cpu_regs[rs]);
        dc->pc += 3;
    }
    gen_helper_itof(cpu_regs[rd], cpu_env, t0);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_FLOAT], cpu_regs[rd]);
    SET_MODE_ZS(RX_PSW_OP_FLOAT);
}

DEFINE_INSN(itof2)
{
    int id, rs, rd, sz, mi;
    TCGv tmp, mem;

    mi = (insn >> 22) & 3;
    id = (insn >> 16) & 3;
    rs = (insn >> 4) & 15;
    rd = insn & 15;
    sz = (mi < 3) ? mi : RX_MEMORY_WORD;

    tmp = tcg_temp_local_new();
    mem = rx_index_addr(id, sz, 4, rs, dc, env);
    if (mi == 3) {
        rx_gen_ldu(RX_MEMORY_WORD, tmp, mem);
    } else {
        rx_gen_ldst(sz, RX_MEMORY_LD, tmp, mem);
    }
    rx_gen_ldst(sz, RX_MEMORY_ST, cpu_regs[rd], mem);
    gen_helper_itof(cpu_regs[rd], cpu_env, tmp);
    tcg_gen_mov_i32(ccop.op_r[RX_PSW_OP_FLOAT], cpu_regs[rd]);
    SET_MODE_ZS(RX_PSW_OP_FLOAT);
    dc->pc += 4 + id;
    tcg_temp_free(mem);
    tcg_temp_free(tmp);
}

DEFINE_INSN(mulmacXX)
{
    int add, lo, rs, rs2;
    TCGv t0, t1;

    add = (insn >> 18) & 1;
    lo = (insn >> 16) & 1;
    rs = (insn >> 12) & 15;
    rs2 = (insn >> 8) & 15;
    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();
    if (lo) {
        tcg_gen_ext16s_i32(t0, cpu_regs[rs]);
        tcg_gen_ext16s_i32(t1, cpu_regs[rs2]);
    } else {
        tcg_gen_sari_i32(t0, cpu_regs[rs], 16);
        tcg_gen_sari_i32(t1, cpu_regs[rs2], 16);
    }
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_mov_i32(t1, t0);
    tcg_gen_shli_i32(t0, t0, 16);
    tcg_gen_sari_i32(t0, t1, 16);
    if (add)
        tcg_gen_add2_i32(cpu_acc_l, cpu_acc_m, cpu_acc_l, cpu_acc_m, t1, t0);
    else {
        tcg_gen_mov_i32(cpu_acc_l, t0);
        tcg_gen_mov_i32(cpu_acc_m, t1);
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    dc->pc += 3;
}

DEFINE_INSN(mvfacXX)
{
    int md, rd;
    TCGv t0;
    md = (insn >> 12) & 3;
    rd = (insn >> 8) & 15;
    if (md == 0) {
        tcg_gen_mov_i32(cpu_regs[rd], cpu_acc_m);
    } else {
        t0 = tcg_temp_local_new();
        tcg_gen_shli_i32(cpu_regs[rd], cpu_acc_m, 16);
        tcg_gen_shri_i32(t0, cpu_acc_l, 16);
        tcg_gen_or_i32(cpu_regs[rd], cpu_regs[rd], t0);
        tcg_temp_free(t0);
    }
    dc->pc += 3;
}

DEFINE_INSN(mvtacXX)
{
    int md, rs;
    md = (insn >> 12) & 3;
    rs = (insn >> 8) & 15;
    if (md == 0) {
        tcg_gen_mov_i32(cpu_acc_m, cpu_regs[rs]);
    } else {
        tcg_gen_mov_i32(cpu_acc_l, cpu_regs[rs]);
    }
    dc->pc += 3;
}

DEFINE_INSN(racw)
{
    TCGv shift;
    shift = tcg_const_local_i32(((insn >> 12) & 1) + 1);
    gen_helper_racw(cpu_env, shift);
    dc->pc += 3;
}

DEFINE_INSN(op0620)
{
    static const disas_proc op[] = {
        adc3sbb2, NULL, adc3sbb2, NULL,
        minmax3, minmax3, emul3, emul3,
        div3, div3, NULL, NULL,
        logic_op4, logic_op4, NULL, NULL,
        xchg2, itof2, NULL, NULL,
        NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL,
    };
    if (op[(insn & 0x00001f00) >> 8]) {
        op[(insn & 0x00001f00) >> 8](env, dc, insn);
    } else {
        gen_helper_raise_illegal_instruction(cpu_env);
    }
}

DEFINE_INSN(opfd70)
{
    static const disas_proc op[] = {
        NULL, NULL, adc1, NULL,
        minmax1, minmax1, emul1, emul1,
        div1, div1, NULL, NULL,
        logic_op2, logic_op2, stz, stz,
    };
    if (op[(insn & 0x0000f000) >> 12]) {
        op[(insn & 0x0000f000) >> 12](env, dc, insn);
    } else {
        gen_helper_raise_illegal_instruction(cpu_env);
    }
}

static disas_proc optable[65536];

#define OPTABLE(code, mask, proc) {code, mask, proc},
static struct op {
    uint16_t code;
    uint16_t mask;
    disas_proc proc;
} oplist[] = {
    OPTABLE(0x0620, 0xff3c, op0620)
    OPTABLE(0xfd70, 0xfff3, opfd70)

    OPTABLE(0x8000, 0xc800, mov1_2)
    OPTABLE(0x8800, 0xc800, mov1_2)
    OPTABLE(0x6600, 0xff00, mov3)
    OPTABLE(0x3c00, 0xfc00, mov4)
    OPTABLE(0x7540, 0xfff0, mov5)
    OPTABLE(0xfb02, 0xff03, mov6)
    OPTABLE(0xcf00, 0xcf00, mov7)
    OPTABLE(0xf800, 0xfc00, mov8)
    OPTABLE(0xcc00, 0xcc00, mov9)
    OPTABLE(0xfe40, 0xffc0, mov10_12)
    OPTABLE(0xc300, 0xc300, mov11)
    OPTABLE(0xfe00, 0xffc0, mov10_12)
    OPTABLE(0xc000, 0xc000, mov13)
    OPTABLE(0xfd20, 0xfff8, mov14)
    OPTABLE(0xfd28, 0xfff8, mov15)

    OPTABLE(0xb000, 0xf000, movu1)
    OPTABLE(0x5800, 0xf800, movu2)
    OPTABLE(0xfec0, 0xffe0, movu3)
    OPTABLE(0xfd30, 0xfff2, movu4)

    OPTABLE(0x7eb0, 0xfff0, pop)
    OPTABLE(0x7ee0, 0xfff0, popc)
    OPTABLE(0x6f00, 0xff00, popm)

    OPTABLE(0x7e80, 0xffc0, push1)
    OPTABLE(0xf408, 0xfc0c, push2)
    OPTABLE(0x7ec0, 0xfff0, pushc)
    OPTABLE(0x6e00, 0xff00, pushm)

    OPTABLE(0xfd67, 0xffff, revl)
    OPTABLE(0xfd65, 0xffff, revw)

    OPTABLE(0xfcd0, 0xfff0, sccnd)

    OPTABLE(0xfc40, 0xffc0, xchg1)

    OPTABLE(0x0300, 0xff00, nop)

    /* and */
    OPTABLE(0x6400, 0xff00, logic_op1)
    OPTABLE(0x7420, 0xfcf0, logic_op2)
    OPTABLE(0x5000, 0xfc00, logic_op3)
    OPTABLE(0x0610, 0xff3c, logic_op4)
    OPTABLE(0xff40, 0xfff0, logic_op5)
    /* or */
    OPTABLE(0x6500, 0xff00, logic_op1)
    OPTABLE(0x7430, 0xfcf0, logic_op2)
    OPTABLE(0x5400, 0xfc00, logic_op3)
    OPTABLE(0x0614, 0xff3c, logic_op4)
    OPTABLE(0xff50, 0xfff0, logic_op5)
    /* xor */
    OPTABLE(0xfc34, 0xfffc, logic_op3)
    /* tst */
    OPTABLE(0xfc30, 0xfffc, logic_op3)

    OPTABLE(0x6200, 0xff00, addsub1)
    OPTABLE(0x4800, 0xfc00, addsub2)
    OPTABLE(0x0608, 0xff3c, addsub3)
    OPTABLE(0x7000, 0xfc00, add4)
    OPTABLE(0xff20, 0xfff0, addsub5)

    OPTABLE(0x6000, 0xff00, addsub1)
    OPTABLE(0x4000, 0xfc00, addsub2)
    OPTABLE(0x0600, 0xff3c, addsub3)
    OPTABLE(0xff00, 0xfff0, addsub5)

    OPTABLE(0x6100, 0xff00, addsub1)
    OPTABLE(0x7550, 0xfff0, cmp2)
    OPTABLE(0x7400, 0xfcf0, cmp3)
    OPTABLE(0x4400, 0xfc00, cmp4)
    OPTABLE(0x0604, 0xff3c, cmp5)

    OPTABLE(0xfc00, 0xfff4, adc2sbb1)

    OPTABLE(0x7e00, 0xffc0, absnegnot1)
    OPTABLE(0xfc03, 0xffc3, absnegnot2)

    OPTABLE(0x6300, 0xff00, mul1)
    OPTABLE(0x7410, 0xfcf0, mul2)
    OPTABLE(0x4c00, 0xfc00, mul3)
    OPTABLE(0x060c, 0xff3c, mul4)
    OPTABLE(0xff30, 0xfff0, mul5)

    OPTABLE(0xfc20, 0xfff8, div2)

    OPTABLE(0xfc18, 0xfff8, emul2)

    OPTABLE(0xfc10, 0xfff8, minmax2)

    OPTABLE(0x6a00, 0xfe00, shift1)
    OPTABLE(0xfd61, 0xffff, shift2)
    OPTABLE(0xfda0, 0xffe0, shift3)
    OPTABLE(0x6c00, 0xfe00, shift1)
    OPTABLE(0xfd62, 0xffff, shift2)
    OPTABLE(0xfdc0, 0xffe0, shift3)
    OPTABLE(0x6800, 0xfe00, shift1)
    OPTABLE(0xfd60, 0xffff, shift2)
    OPTABLE(0xfd80, 0xffe0, shift3)

    OPTABLE(0x7e40, 0xffe0, roc)
    OPTABLE(0xfd6e, 0xfffe, rot1)
    OPTABLE(0xfd66, 0xffff, rot2)
    OPTABLE(0xfd6c, 0xfffe, rot1)
    OPTABLE(0xfd64, 0xffff, rot2)

    OPTABLE(0x7e30, 0xfff0, sat)
    OPTABLE(0x7f93, 0xffff, satr)
    OPTABLE(0x7f8c, 0xfffc, rmpa)

    OPTABLE(0xf008, 0xfc08, bop1)
    OPTABLE(0xfc64, 0xfffc, bop2)
    OPTABLE(0x7a00, 0xfe00, bop3)
    OPTABLE(0xfce0, 0xffe0, bnot1)
    OPTABLE(0xfc6c, 0xfffc, bop2)
    OPTABLE(0xf000, 0xfc08, bop1)
    OPTABLE(0xfc60, 0xfffc, bop2)
    OPTABLE(0x7800, 0xfe00, bop3)
    OPTABLE(0xf400, 0xfc08, bop1)
    OPTABLE(0xfc68, 0xfffc, bop2)
    OPTABLE(0x7c00, 0xfe00, bop3)

    OPTABLE(0xfce0, 0xffe0, bmcnd1)
    OPTABLE(0xfde0, 0xffe0, bmcnd2)

    OPTABLE(0x7f83, 0xffff, scmpu)
    OPTABLE(0x7f8b, 0xffff, smovbfu)
    OPTABLE(0x7f8f, 0xffff, smovbfu)
    OPTABLE(0x7f87, 0xffff, smovbfu)
    OPTABLE(0x7f88, 0xfffc, sstr)
    OPTABLE(0x7f80, 0xfffc, ssearch)
    OPTABLE(0x7f84, 0xfffc, ssearch)

    OPTABLE(0x0800, 0xf800, bra1)
    OPTABLE(0x2e00, 0xff00, bra2)
    OPTABLE(0x3800, 0xff00, bra3)
    OPTABLE(0x0400, 0xff00, bra4)
    OPTABLE(0x7f40, 0xfff0, bra5)

    OPTABLE(0x1000, 0xf000, bcnd1)
    OPTABLE(0x2000, 0xf000, bcnd2)
    OPTABLE(0x3a00, 0xfe00, bcnd3)

    OPTABLE(0x3900, 0xff00, bsr1)
    OPTABLE(0x0500, 0xff00, bsr2)
    OPTABLE(0x7f50, 0xfff0, bsr3)

    OPTABLE(0x7f00, 0xfff0, jmpjsr)
    OPTABLE(0x7f10, 0xfff0, jmpjsr)

    OPTABLE(0x0200, 0xff00, rts)
    OPTABLE(0x6700, 0xff00, rtsd1)
    OPTABLE(0x3f00, 0xff00, rtsd2)

    OPTABLE(0x7fb0, 0xfff0, clrsetpsw)
    OPTABLE(0x7fa0, 0xfff0, clrsetpsw)

    OPTABLE(0xfd6a, 0xffff, mvfc)
    OPTABLE(0xfd73, 0xfff3, mvtc1)
    OPTABLE(0xfd68, 0xfff8, mvtc2)
    OPTABLE(0x7570, 0xffff, mvtipl)

    OPTABLE(0x0000, 0xff00, rxbrk)
    OPTABLE(0x7560, 0xffff, rxint)

    OPTABLE(0x7f95, 0xffff, rte)
    OPTABLE(0x7f94, 0xffff, rtfi)
    OPTABLE(0x7f96, 0xffff, rxwait)

    OPTABLE(0xfd72, 0xffff, fimm)
    OPTABLE(0xfc88, 0xfffc, fmem)
    OPTABLE(0xfc84, 0xfffc, fmem)
    OPTABLE(0xfc90, 0xfffc, fmem)
    OPTABLE(0xfc8c, 0xfffc, fmem)
    OPTABLE(0xfc80, 0xfffc, fmem)
    OPTABLE(0xfc94, 0xfffc, fmem)
    OPTABLE(0xfc98, 0xfffc, fmem)
    OPTABLE(0xfc44, 0xfffc, itof1)

    OPTABLE(0xfd04, 0xffff, mulmacXX)
    OPTABLE(0xfd05, 0xffff, mulmacXX)
    OPTABLE(0xfd00, 0xffff, mulmacXX)
    OPTABLE(0xfd01, 0xffff, mulmacXX)
    OPTABLE(0xfd1f, 0xffff, mvfacXX)
    OPTABLE(0xfd17, 0xffff, mvtacXX)
    OPTABLE(0xfd18, 0xffff, racw)
};

static int comp_mask(const void *p1, const void *p2)
{
    return ctpop32(((struct op *)p1)->mask)
        - ctpop32(((struct op *)p2)->mask);
}

static void rx_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
}

static void rx_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void rx_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(dc->base.pc_next);
}

static bool rx_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cs,
                                    const CPUBreakpoint *bp)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    /* We have hit a breakpoint - make sure PC is up-to-date */
    gen_save_cpu_state(dc, true);
    gen_helper_debug(cpu_env);
    dc->base.is_jmp = DISAS_NORETURN;
    dc->base.pc_next += 1;
    return true;
}

static void rx_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    CPURXState *env = cs->env_ptr;
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    uint32_t insn = 0;
    int i;

    for (i = 0; i < 4; i++) {
        insn <<= 8;
        insn |= cpu_ldub_code(env, dc->base.pc_next + i);
    }
    dc->pc = dc->base.pc_next;
    if (optable[insn >> 16]) {
        optable[insn >> 16](env, dc, insn);
        dc->base.pc_next = dc->pc;
    } else {
        gen_helper_raise_illegal_instruction(cpu_env);
    }
}

static void rx_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        gen_save_cpu_state(dc, false);
        gen_goto_tb(dc, 0, dc->base.pc_next);
        break;
    case DISAS_JUMP:
        if (dc->base.singlestep_enabled) {
            gen_helper_update_psw(cpu_env);
            gen_helper_debug(cpu_env);
        } else
            tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void rx_tr_disas_log(const DisasContextBase *dcbase, CPUState *cs)
{
    qemu_log("IN:\n");  /* , lookup_symbol(dcbase->pc_first)); */
    log_target_disas(cs, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps rx_tr_ops = {
    .init_disas_context = rx_tr_init_disas_context,
    .tb_start           = rx_tr_tb_start,
    .insn_start         = rx_tr_insn_start,
    .breakpoint_check   = rx_tr_breakpoint_check,
    .translate_insn     = rx_tr_translate_insn,
    .tb_stop            = rx_tr_tb_stop,
    .disas_log          = rx_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb)
{
    DisasContext dc;

    translator_loop(&rx_tr_ops, &dc.base, cs, tb);
}

void restore_state_to_opc(CPURXState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
    env->psw = data[1];
    rx_cpu_unpack_psw(env, 1);
}

#define ALLOC_REGISTER(sym, name) \
    cpu_##sym = tcg_global_mem_new_i32(cpu_env, \
                                       offsetof(CPURXState, sym), name)

void rx_translate_init(void)
{
    int i, j;
    struct op *p;
    static const char * const regnames[16] = {
        "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
    };

    for (i = 0; i < 16; i++) {
        cpu_regs[i] = tcg_global_mem_new_i32(cpu_env,
                                              offsetof(CPURXState, regs[i]),
                                              regnames[i]);
    }
    for (i = 0; i < 12; i++) {
        ccop.op_a1[i + 1] = tcg_global_mem_new_i32(cpu_env,
                                               offsetof(CPURXState, op_a1[i]),
                                               "");
        ccop.op_a2[i + 1] = tcg_global_mem_new_i32(cpu_env,
                                               offsetof(CPURXState, op_a2[i]),
                                               "");
        ccop.op_r[i + 1] = tcg_global_mem_new_i32(cpu_env,
                                               offsetof(CPURXState, op_r[i]),
                                               "");
    }
    ccop.op_mode = tcg_global_mem_new_i32(cpu_env,
                                          offsetof(CPURXState, op_mode),
                                          "");
    ALLOC_REGISTER(pc, "PC");
    ALLOC_REGISTER(psw, "PSW");
    ALLOC_REGISTER(psw_o, "PSW(O)");
    ALLOC_REGISTER(psw_s, "PSW(S)");
    ALLOC_REGISTER(psw_z, "PSW(Z)");
    ALLOC_REGISTER(psw_c, "PSW(C)");
    ALLOC_REGISTER(psw_u, "PSW(U)");
    ALLOC_REGISTER(psw_i, "PSW(I)");
    ALLOC_REGISTER(psw_pm, "PSW(PM)");
    ALLOC_REGISTER(psw_ipl, "PSW(IPL)");
    ALLOC_REGISTER(usp, "USP");
    ALLOC_REGISTER(fpsw, "FPSW");
    ALLOC_REGISTER(bpsw, "BPSW");
    ALLOC_REGISTER(bpc, "BPC");
    ALLOC_REGISTER(isp, "ISP");
    ALLOC_REGISTER(fintv, "FINTV");
    ALLOC_REGISTER(intb, "INTB");
    ALLOC_REGISTER(acc_m, "ACC-M");
    ALLOC_REGISTER(acc_l, "ACC-L");

    qsort(oplist, ARRAY_SIZE(oplist), sizeof(struct op), comp_mask);
    for (p = oplist, i = 0; i < ARRAY_SIZE(oplist); p++, i++) {
        for (j = 0; j < 0x10000; j++) {
            if (p->code == (j & p->mask)) {
                optable[j] = p->proc;
            }
        }
    }
}
