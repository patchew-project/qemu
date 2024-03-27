/*
 * Altera Nios II emulation for qemu: main translation routines.
 *
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 *  (Portions of this file that were originally from nios2sim-ng.)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "qemu/qemu-print.h"
#include "semihosting/semihost.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H


/* is_jmp field values */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */

#define INSTRUCTION_FLG(func, flags) { (func), (flags) }
#define INSTRUCTION(func)                  \
        INSTRUCTION_FLG(func, 0)
#define INSTRUCTION_NOP()                  \
        INSTRUCTION_FLG(nop, 0)
#define INSTRUCTION_UNIMPLEMENTED()        \
        INSTRUCTION_FLG(gen_excp, EXCP_UNIMPL)
#define INSTRUCTION_ILLEGAL()              \
        INSTRUCTION_FLG(gen_excp, EXCP_ILLEGAL)
#define INSTRUCTION_SUPERVISOR()              \
        INSTRUCTION_FLG(gen_excp, EXCP_SUPERI)

/* Special R-Type instruction opcode */
#define INSN_R_TYPE 0x3A

/* I-Type instruction parsing */
typedef struct {
    uint8_t op;
    union {
        uint16_t u;
        int16_t s;
    } imm16;
    uint8_t b;
    uint8_t a;
} InstrIType;

#define I_TYPE(instr, code)                \
    InstrIType (instr) = {                 \
        .op    = extract32((code), 0, 6),  \
        .imm16.u = extract32((code), 6, 16), \
        .b     = extract32((code), 22, 5), \
        .a     = extract32((code), 27, 5), \
    }

typedef target_ulong ImmFromIType(const InstrIType *);

static target_ulong imm_unsigned(const InstrIType *i)
{
    return i->imm16.u;
}

static target_ulong imm_signed(const InstrIType *i)
{
    return i->imm16.s;
}

static target_ulong imm_shifted(const InstrIType *i)
{
    return i->imm16.u << 16;
}

/* R-Type instruction parsing */
typedef struct {
    uint8_t op;
    uint8_t imm5;
    uint8_t opx;
    uint8_t c;
    uint8_t b;
    uint8_t a;
} InstrRType;

#define R_TYPE(instr, code)                \
    InstrRType (instr) = {                 \
        .op    = extract32((code), 0, 6),  \
        .imm5  = extract32((code), 6, 5),  \
        .opx   = extract32((code), 11, 6), \
        .c     = extract32((code), 17, 5), \
        .b     = extract32((code), 22, 5), \
        .a     = extract32((code), 27, 5), \
    }

/* J-Type instruction parsing */
typedef struct {
    uint8_t op;
    uint32_t imm26;
} InstrJType;

#define J_TYPE(instr, code)                \
    InstrJType (instr) = {                 \
        .op    = extract32((code), 0, 6),  \
        .imm26 = extract32((code), 6, 26), \
    }

typedef void GenFn2i(TCGv, TCGv, target_long);
typedef void GenFn3(TCGv, TCGv, TCGv);
typedef void GenFn4(TCGv, TCGv, TCGv, TCGv);

typedef struct DisasContext {
    DisasContextBase  base;
    target_ulong      pc;
    int               mem_idx;
    uint32_t          tb_flags;
    TCGv              sink;
    const ControlRegState *cr_state;
} DisasContext;

static TCGv cpu_R[NUM_GP_REGS];
static TCGv cpu_pc;

typedef struct Nios2Instruction {
    void     (*handler)(DisasContext *dc, uint32_t code, uint32_t flags);
    uint32_t  flags;
} Nios2Instruction;

static uint8_t get_opcode(uint32_t code)
{
    I_TYPE(instr, code);
    return instr.op;
}

static uint8_t get_opxcode(uint32_t code)
{
    R_TYPE(instr, code);
    return instr.opx;
}

static TCGv load_gpr(DisasContext *dc, unsigned reg)
{
    assert(reg < NUM_GP_REGS);

    /*
     * With shadow register sets, register r0 does not necessarily contain 0,
     * but it is overwhelmingly likely that it does -- software is supposed
     * to have set r0 to 0 in every shadow register set before use.
     */
    if (unlikely(reg == R_ZERO) && FIELD_EX32(dc->tb_flags, TBFLAGS, R0_0)) {
        return tcg_constant_tl(0);
    }
    if (FIELD_EX32(dc->tb_flags, TBFLAGS, CRS0)) {
        return cpu_R[reg];
    }
    g_assert_not_reached();
}

static TCGv dest_gpr(DisasContext *dc, unsigned reg)
{
    assert(reg < NUM_GP_REGS);

    /*
     * The spec for shadow register sets isn't clear, but we assume that
     * writes to r0 are discarded regardless of CRS.
     */
    if (unlikely(reg == R_ZERO)) {
        if (dc->sink == NULL) {
            dc->sink = tcg_temp_new();
        }
        return dc->sink;
    }
    if (FIELD_EX32(dc->tb_flags, TBFLAGS, CRS0)) {
        return cpu_R[reg];
    }
    g_assert_not_reached();
}

static void t_gen_helper_raise_exception(DisasContext *dc, uint32_t index)
{
    /* Note that PC is advanced for all hardware exceptions. */
    tcg_gen_movi_tl(cpu_pc, dc->base.pc_next);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(index));
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_goto_tb(DisasContext *dc, int n, uint32_t dest)
{
    const TranslationBlock *tb = dc->base.tb;

    if (translator_use_goto_tb(&dc->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb(tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_jumpr(DisasContext *dc, int regno, bool is_call)
{
    TCGLabel *l = gen_new_label();
    TCGv test = tcg_temp_new();
    TCGv dest = load_gpr(dc, regno);

    tcg_gen_andi_tl(test, dest, 3);
    tcg_gen_brcondi_tl(TCG_COND_NE, test, 0, l);

    tcg_gen_mov_tl(cpu_pc, dest);
    if (is_call) {
        tcg_gen_movi_tl(dest_gpr(dc, R_RA), dc->base.pc_next);
    }
    tcg_gen_lookup_and_goto_ptr();

    gen_set_label(l);
    tcg_gen_st_tl(dest, tcg_env, offsetof(CPUNios2State, ctrl[CR_BADADDR]));
    t_gen_helper_raise_exception(dc, EXCP_UNALIGND);

    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_excp(DisasContext *dc, uint32_t code, uint32_t flags)
{
    t_gen_helper_raise_exception(dc, flags);
}

static bool gen_check_supervisor(DisasContext *dc)
{
    if (FIELD_EX32(dc->tb_flags, TBFLAGS, U)) {
        /* CPU in user mode, privileged instruction called, stop. */
        t_gen_helper_raise_exception(dc, EXCP_SUPERI);
        return false;
    }
    return true;
}

/*
 * Used as a placeholder for all instructions which do not have
 * an effect on the simulator (e.g. flush, sync)
 */
static void nop(DisasContext *dc, uint32_t code, uint32_t flags)
{
    /* Nothing to do here */
}

/*
 * J-Type instructions
 */
static void jmpi(DisasContext *dc, uint32_t code, uint32_t flags)
{
    J_TYPE(instr, code);
    gen_goto_tb(dc, 0, (dc->pc & 0xF0000000) | (instr.imm26 << 2));
}

static void call(DisasContext *dc, uint32_t code, uint32_t flags)
{
    tcg_gen_movi_tl(dest_gpr(dc, R_RA), dc->base.pc_next);
    jmpi(dc, code, flags);
}

/*
 * I-Type instructions
 */
/* Load instructions */
static void gen_ldx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);

    TCGv addr = tcg_temp_new();
    TCGv data = dest_gpr(dc, instr.b);

    tcg_gen_addi_tl(addr, load_gpr(dc, instr.a), instr.imm16.s);
    flags |= MO_UNALN;
    tcg_gen_qemu_ld_tl(data, addr, dc->mem_idx, flags);
}

/* Store instructions */
static void gen_stx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);
    TCGv val = load_gpr(dc, instr.b);

    TCGv addr = tcg_temp_new();
    tcg_gen_addi_tl(addr, load_gpr(dc, instr.a), instr.imm16.s);
    flags |= MO_UNALN;
    tcg_gen_qemu_st_tl(val, addr, dc->mem_idx, flags);
}

/* Branch instructions */
static void br(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);

    gen_goto_tb(dc, 0, dc->base.pc_next + (instr.imm16.s & -4));
}

static void gen_bxx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);

    TCGLabel *l1 = gen_new_label();
    tcg_gen_brcond_tl(flags, load_gpr(dc, instr.a), load_gpr(dc, instr.b), l1);
    gen_goto_tb(dc, 0, dc->base.pc_next);
    gen_set_label(l1);
    gen_goto_tb(dc, 1, dc->base.pc_next + (instr.imm16.s & -4));
}

/* Comparison instructions */
static void do_i_cmpxx(DisasContext *dc, uint32_t insn,
                       TCGCond cond, ImmFromIType *imm)
{
    I_TYPE(instr, insn);
    tcg_gen_setcondi_tl(cond, dest_gpr(dc, instr.b),
                        load_gpr(dc, instr.a), imm(&instr));
}

#define gen_i_cmpxx(fname, imm)                                             \
    static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)    \
    { do_i_cmpxx(dc, code, flags, imm); }

gen_i_cmpxx(gen_cmpxxsi, imm_signed)
gen_i_cmpxx(gen_cmpxxui, imm_unsigned)

/* Math/logic instructions */
static void do_i_math_logic(DisasContext *dc, uint32_t insn,
                            GenFn2i *fn, ImmFromIType *imm,
                            bool x_op_0_eq_x)
{
    I_TYPE(instr, insn);
    target_ulong val;

    if (unlikely(instr.b == R_ZERO)) {
        /* Store to R_ZERO is ignored -- this catches the canonical NOP. */
        return;
    }

    val = imm(&instr);

    if (instr.a == R_ZERO && FIELD_EX32(dc->tb_flags, TBFLAGS, R0_0)) {
        /* This catches the canonical expansions of movi and movhi. */
        tcg_gen_movi_tl(dest_gpr(dc, instr.b), x_op_0_eq_x ? val : 0);
    } else {
        fn(dest_gpr(dc, instr.b), load_gpr(dc, instr.a), val);
    }
}

#define gen_i_math_logic(fname, insn, x_op_0, imm)                          \
    static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)    \
    { do_i_math_logic(dc, code, tcg_gen_##insn##_tl, imm, x_op_0); }

gen_i_math_logic(addi,  addi, 1, imm_signed)
gen_i_math_logic(muli,  muli, 0, imm_signed)

gen_i_math_logic(andi,  andi, 0, imm_unsigned)
gen_i_math_logic(ori,   ori,  1, imm_unsigned)
gen_i_math_logic(xori,  xori, 1, imm_unsigned)

gen_i_math_logic(andhi, andi, 0, imm_shifted)
gen_i_math_logic(orhi , ori,  1, imm_shifted)
gen_i_math_logic(xorhi, xori, 1, imm_shifted)

/* Prototype only, defined below */
static void handle_r_type_instr(DisasContext *dc, uint32_t code,
                                uint32_t flags);

static const Nios2Instruction i_type_instructions[] = {
    INSTRUCTION(call),                                /* call */
    INSTRUCTION(jmpi),                                /* jmpi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_UB),                  /* ldbu */
    INSTRUCTION(addi),                                /* addi */
    INSTRUCTION_FLG(gen_stx, MO_UB),                  /* stb */
    INSTRUCTION(br),                                  /* br */
    INSTRUCTION_FLG(gen_ldx, MO_SB),                  /* ldb */
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_GE),        /* cmpgei */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_TEUW),                /* ldhu */
    INSTRUCTION(andi),                                /* andi */
    INSTRUCTION_FLG(gen_stx, MO_TEUW),                /* sth */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_GE),            /* bge */
    INSTRUCTION_FLG(gen_ldx, MO_TESW),                /* ldh */
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_LT),        /* cmplti */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_NOP(),                                /* initda */
    INSTRUCTION(ori),                                 /* ori */
    INSTRUCTION_FLG(gen_stx, MO_TEUL),                /* stw */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_LT),            /* blt */
    INSTRUCTION_FLG(gen_ldx, MO_TEUL),                /* ldw */
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_NE),        /* cmpnei */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_NOP(),                                /* flushda */
    INSTRUCTION(xori),                                /* xori */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_bxx, TCG_COND_NE),            /* bne */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_EQ),        /* cmpeqi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_UB),                  /* ldbuio */
    INSTRUCTION(muli),                                /* muli */
    INSTRUCTION_FLG(gen_stx, MO_UB),                  /* stbio */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_EQ),            /* beq */
    INSTRUCTION_FLG(gen_ldx, MO_SB),                  /* ldbio */
    INSTRUCTION_FLG(gen_cmpxxui, TCG_COND_GEU),       /* cmpgeui */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_TEUW),                /* ldhuio */
    INSTRUCTION(andhi),                               /* andhi */
    INSTRUCTION_FLG(gen_stx, MO_TEUW),                /* sthio */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_GEU),           /* bgeu */
    INSTRUCTION_FLG(gen_ldx, MO_TESW),                /* ldhio */
    INSTRUCTION_FLG(gen_cmpxxui, TCG_COND_LTU),       /* cmpltui */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_UNIMPLEMENTED(),                      /* custom */
    INSTRUCTION_NOP(),                                /* initd */
    INSTRUCTION(orhi),                                /* orhi */
    INSTRUCTION_FLG(gen_stx, MO_TESL),                /* stwio */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_LTU),           /* bltu */
    INSTRUCTION_FLG(gen_ldx, MO_TEUL),                /* ldwio */
    INSTRUCTION_SUPERVISOR(),                         /* rdprs */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(handle_r_type_instr, 0),          /* R-Type */
    INSTRUCTION_NOP(),                                /* flushd */
    INSTRUCTION(xorhi),                               /* xorhi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
};

/*
 * R-Type instructions
 */

/* PC <- ra */
static void ret(DisasContext *dc, uint32_t code, uint32_t flags)
{
    gen_jumpr(dc, R_RA, false);
}

/*
 * status <- bstatus
 * PC <- ba
 */
static void bret(DisasContext *dc, uint32_t code, uint32_t flags)
{
    if (!gen_check_supervisor(dc)) {
        return;
    }

    g_assert_not_reached();
}

/* PC <- rA */
static void jmp(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    gen_jumpr(dc, instr.a, false);
}

/* rC <- PC + 4 */
static void nextpc(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    tcg_gen_movi_tl(dest_gpr(dc, instr.c), dc->base.pc_next);
}

/*
 * ra <- PC + 4
 * PC <- rA
 */
static void callr(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    gen_jumpr(dc, instr.a, true);
}

/* rC <- ctlN */
static void rdctl(DisasContext *dc, uint32_t code, uint32_t flags)
{
    if (!gen_check_supervisor(dc)) {
        return;
    }

    g_assert_not_reached();
}

/* ctlN <- rA */
static void wrctl(DisasContext *dc, uint32_t code, uint32_t flags)
{
    if (!gen_check_supervisor(dc)) {
        return;
    }

    g_assert_not_reached();
}

/* Comparison instructions */
static void gen_cmpxx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);
    tcg_gen_setcond_tl(flags, dest_gpr(dc, instr.c),
                       load_gpr(dc, instr.a), load_gpr(dc, instr.b));
}

/* Math/logic instructions */
static void do_ri_math_logic(DisasContext *dc, uint32_t insn, GenFn2i *fn)
{
    R_TYPE(instr, insn);
    fn(dest_gpr(dc, instr.c), load_gpr(dc, instr.a), instr.imm5);
}

static void do_rr_math_logic(DisasContext *dc, uint32_t insn, GenFn3 *fn)
{
    R_TYPE(instr, insn);
    fn(dest_gpr(dc, instr.c), load_gpr(dc, instr.a), load_gpr(dc, instr.b));
}

#define gen_ri_math_logic(fname, insn)                                      \
    static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)    \
    { do_ri_math_logic(dc, code, tcg_gen_##insn##_tl); }

#define gen_rr_math_logic(fname, insn)                                      \
    static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)    \
    { do_rr_math_logic(dc, code, tcg_gen_##insn##_tl); }

gen_rr_math_logic(add,  add)
gen_rr_math_logic(sub,  sub)
gen_rr_math_logic(mul,  mul)

gen_rr_math_logic(and,  and)
gen_rr_math_logic(or,   or)
gen_rr_math_logic(xor,  xor)
gen_rr_math_logic(nor,  nor)

gen_ri_math_logic(srai, sari)
gen_ri_math_logic(srli, shri)
gen_ri_math_logic(slli, shli)
gen_ri_math_logic(roli, rotli)

static void do_rr_mul_high(DisasContext *dc, uint32_t insn, GenFn4 *fn)
{
    R_TYPE(instr, insn);
    TCGv discard = tcg_temp_new();

    fn(discard, dest_gpr(dc, instr.c),
       load_gpr(dc, instr.a), load_gpr(dc, instr.b));
}

#define gen_rr_mul_high(fname, insn)                                        \
    static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)    \
    { do_rr_mul_high(dc, code, tcg_gen_##insn##_tl); }

gen_rr_mul_high(mulxss, muls2)
gen_rr_mul_high(mulxuu, mulu2)
gen_rr_mul_high(mulxsu, mulsu2)

static void do_rr_shift(DisasContext *dc, uint32_t insn, GenFn3 *fn)
{
    R_TYPE(instr, insn);
    TCGv sh = tcg_temp_new();

    tcg_gen_andi_tl(sh, load_gpr(dc, instr.b), 31);
    fn(dest_gpr(dc, instr.c), load_gpr(dc, instr.a), sh);
}

#define gen_rr_shift(fname, insn)                                           \
    static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)    \
    { do_rr_shift(dc, code, tcg_gen_##insn##_tl); }

gen_rr_shift(sra, sar)
gen_rr_shift(srl, shr)
gen_rr_shift(sll, shl)
gen_rr_shift(rol, rotl)
gen_rr_shift(ror, rotr)

static void divs(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, (code));
    gen_helper_divs(dest_gpr(dc, instr.c), tcg_env,
                    load_gpr(dc, instr.a), load_gpr(dc, instr.b));
}

static void divu(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, (code));
    gen_helper_divu(dest_gpr(dc, instr.c), tcg_env,
                    load_gpr(dc, instr.a), load_gpr(dc, instr.b));
}

static void trap(DisasContext *dc, uint32_t code, uint32_t flags)
{
    /*
     * The imm5 field is not stored anywhere on real hw; the kernel
     * has to load the insn and extract the field.  But we can make
     * things easier for cpu_loop if we pop this into env->error_code.
     */
    R_TYPE(instr, code);
    tcg_gen_st_i32(tcg_constant_i32(instr.imm5), tcg_env,
                   offsetof(CPUNios2State, error_code));
    t_gen_helper_raise_exception(dc, EXCP_TRAP);
}

static void gen_break(DisasContext *dc, uint32_t code, uint32_t flags)
{
    t_gen_helper_raise_exception(dc, EXCP_BREAK);
}

static const Nios2Instruction r_type_instructions[] = {
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_SUPERVISOR(),                         /* eret */
    INSTRUCTION(roli),                                /* roli */
    INSTRUCTION(rol),                                 /* rol */
    INSTRUCTION_NOP(),                                /* flushp */
    INSTRUCTION(ret),                                 /* ret */
    INSTRUCTION(nor),                                 /* nor */
    INSTRUCTION(mulxuu),                              /* mulxuu */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_GE),          /* cmpge */
    INSTRUCTION(bret),                                /* bret */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(ror),                                 /* ror */
    INSTRUCTION_NOP(),                                /* flushi */
    INSTRUCTION(jmp),                                 /* jmp */
    INSTRUCTION(and),                                 /* and */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_LT),          /* cmplt */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(slli),                                /* slli */
    INSTRUCTION(sll),                                 /* sll */
    INSTRUCTION_ILLEGAL(),                            /* wrprs */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(or),                                  /* or */
    INSTRUCTION(mulxsu),                              /* mulxsu */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_NE),          /* cmpne */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(srli),                                /* srli */
    INSTRUCTION(srl),                                 /* srl */
    INSTRUCTION(nextpc),                              /* nextpc */
    INSTRUCTION(callr),                               /* callr */
    INSTRUCTION(xor),                                 /* xor */
    INSTRUCTION(mulxss),                              /* mulxss */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_EQ),          /* cmpeq */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(divu),                                /* divu */
    INSTRUCTION(divs),                                /* div */
    INSTRUCTION(rdctl),                               /* rdctl */
    INSTRUCTION(mul),                                 /* mul */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_GEU),         /* cmpgeu */
    INSTRUCTION_NOP(),                                /* initi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(trap),                                /* trap */
    INSTRUCTION(wrctl),                               /* wrctl */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_LTU),         /* cmpltu */
    INSTRUCTION(add),                                 /* add */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(gen_break),                           /* break */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(nop),                                 /* nop */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(sub),                                 /* sub */
    INSTRUCTION(srai),                                /* srai */
    INSTRUCTION(sra),                                 /* sra */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
};

static void handle_r_type_instr(DisasContext *dc, uint32_t code, uint32_t flags)
{
    uint8_t opx;
    const Nios2Instruction *instr;

    opx = get_opxcode(code);
    if (unlikely(opx >= ARRAY_SIZE(r_type_instructions))) {
        goto illegal_op;
    }

    instr = &r_type_instructions[opx];
    instr->handler(dc, code, instr->flags);

    return;

illegal_op:
    t_gen_helper_raise_exception(dc, EXCP_ILLEGAL);
}

static const char * const gr_regnames[NUM_GP_REGS] = {
    "zero",       "at",         "r2",         "r3",
    "r4",         "r5",         "r6",         "r7",
    "r8",         "r9",         "r10",        "r11",
    "r12",        "r13",        "r14",        "r15",
    "r16",        "r17",        "r18",        "r19",
    "r20",        "r21",        "r22",        "r23",
    "et",         "bt",         "gp",         "sp",
    "fp",         "ea",         "ba",         "ra",
};

/* generate intermediate code for basic block 'tb'.  */
static void nios2_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUNios2State *env = cpu_env(cs);
    Nios2CPU *cpu = env_archcpu(env);
    int page_insns;

    dc->mem_idx = cpu_mmu_index(cs, false);
    dc->cr_state = cpu->cr_state;
    dc->tb_flags = dc->base.tb->flags;

    /* Bound the number of insns to execute to those left on the page.  */
    page_insns = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;
    dc->base.max_insns = MIN(page_insns, dc->base.max_insns);
}

static void nios2_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void nios2_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    tcg_gen_insn_start(dcbase->pc_next);
}

static void nios2_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    const Nios2Instruction *instr;
    uint32_t code, pc;
    uint8_t op;

    pc = dc->base.pc_next;
    dc->pc = pc;
    dc->base.pc_next = pc + 4;

    /* Decode an instruction */
    code = cpu_ldl_code(cpu_env(cs), pc);
    op = get_opcode(code);

    if (unlikely(op >= ARRAY_SIZE(i_type_instructions))) {
        t_gen_helper_raise_exception(dc, EXCP_ILLEGAL);
        return;
    }

    dc->sink = NULL;

    instr = &i_type_instructions[op];
    instr->handler(dc, code, instr->flags);
}

static void nios2_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    /* Indicate where the next block should start */
    switch (dc->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_goto_tb(dc, 0, dc->base.pc_next);
        break;

    case DISAS_UPDATE:
        /* Save the current PC, and return to the main loop. */
        tcg_gen_movi_tl(cpu_pc, dc->base.pc_next);
        tcg_gen_exit_tb(NULL, 0);
        break;

    case DISAS_NORETURN:
        /* nothing more to generate */
        break;

    default:
        g_assert_not_reached();
    }
}

static void nios2_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps nios2_tr_ops = {
    .init_disas_context = nios2_tr_init_disas_context,
    .tb_start           = nios2_tr_tb_start,
    .insn_start         = nios2_tr_insn_start,
    .translate_insn     = nios2_tr_translate_insn,
    .tb_stop            = nios2_tr_tb_stop,
    .disas_log          = nios2_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                           vaddr pc, void *host_pc)
{
    DisasContext dc;
    translator_loop(cs, tb, max_insns, pc, host_pc, &nios2_tr_ops, &dc.base);
}

void nios2_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    int i;

    qemu_fprintf(f, "IN: PC=%x %s\n", env->pc, lookup_symbol(env->pc));

    for (i = 0; i < NUM_GP_REGS; i++) {
        qemu_fprintf(f, "%9s=%8.8x ", gr_regnames[i], env->regs[i]);
        if ((i + 1) % 4 == 0) {
            qemu_fprintf(f, "\n");
        }
    }

    qemu_fprintf(f, "\n\n");
}

void nios2_tcg_init(void)
{
#define offsetof_regs0(N)  offsetof(CPUNios2State, regs[N])

    for (int i = 0; i < NUM_GP_REGS; i++) {
        cpu_R[i] = tcg_global_mem_new(tcg_env, offsetof_regs0(i),
                                      gr_regnames[i]);
    }

#undef offsetof_regs0

    cpu_pc = tcg_global_mem_new(tcg_env,
                                offsetof(CPUNios2State, pc), "pc");
}
