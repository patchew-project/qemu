/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "qemu/qemu-print.h"
#include "tcg/tcg-op-gvec.h"
#include "target/arc/semfunc.h"
#include "target/arc/arc-common.h"

/* Globals */
TCGv    cpu_gp;        /*  Global Pointer                      */
TCGv    cpu_fp;        /*  Frame Pointer                       */
TCGv    cpu_sp;        /*  Stack Pointer                       */
TCGv    cpu_ilink1;    /*  Level 1 interrupt link register     */
TCGv    cpu_ilink2;    /*  Level 2 interrupt link register     */
TCGv    cpu_blink;     /*  Branch link register                */
TCGv    cpu_acclo;     /*  64-bit accumulator register: low    */
TCGv    cpu_acchi;     /*  64-bit accumulator register: high   */
TCGv    cpu_limm;      /*  Long immediate data indicator       */
TCGv    cpu_pcl;       /*  Program Counter [31:2], read-only.  */

TCGv    cpu_S1f;
TCGv    cpu_S2f;
TCGv    cpu_CSf;

TCGv    cpu_Lf;
TCGv    cpu_Zf;
TCGv    cpu_Nf;
TCGv    cpu_Cf;
TCGv    cpu_Vf;
TCGv    cpu_Uf;

TCGv    cpu_DEf;
TCGv    cpu_ESf;
TCGv    cpu_AEf;
TCGv    cpu_Hf;
TCGv    cpu_IEf;
TCGv    cpu_Ef;

TCGv    cpu_is_delay_slot_instruction;

TCGv    cpu_l1_Lf;
TCGv    cpu_l1_Zf;
TCGv    cpu_l1_Nf;
TCGv    cpu_l1_Cf;
TCGv    cpu_l1_Vf;
TCGv    cpu_l1_Uf;

TCGv    cpu_l1_DEf;
TCGv    cpu_l1_AEf;
TCGv    cpu_l1_Hf;

TCGv    cpu_l2_Lf;
TCGv    cpu_l2_Zf;
TCGv    cpu_l2_Nf;
TCGv    cpu_l2_Cf;
TCGv    cpu_l2_Vf;
TCGv    cpu_l2_Uf;

TCGv    cpu_l2_DEf;
TCGv    cpu_l2_AEf;
TCGv    cpu_l2_Hf;

TCGv    cpu_er_Lf;
TCGv    cpu_er_Zf;
TCGv    cpu_er_Nf;
TCGv    cpu_er_Cf;
TCGv    cpu_er_Vf;
TCGv    cpu_er_Uf;

TCGv    cpu_er_DEf;
TCGv    cpu_er_AEf;
TCGv    cpu_er_Hf;

TCGv    cpu_eret;
TCGv    cpu_erbta;
TCGv    cpu_ecr;
TCGv    cpu_efa;

TCGv    cpu_bta;
TCGv    cpu_bta_l1;
TCGv    cpu_bta_l2;

TCGv    cpu_pc;
TCGv    cpu_lpc;
/* replaced by AUX_REG array */
TCGv    cpu_lps;
TCGv    cpu_lpe;

TCGv    cpu_r[64];

TCGv    cpu_intvec;

TCGv    cpu_debug_LD;
TCGv    cpu_debug_SH;
TCGv    cpu_debug_BH;
TCGv    cpu_debug_UB;
TCGv    cpu_debug_ZZ;
TCGv    cpu_debug_RA;
TCGv    cpu_debug_IS;
TCGv    cpu_debug_FH;
TCGv    cpu_debug_SS;

TCGv    cpu_lock_lf_var;

/* NOTE: Pseudo register required for comparison with lp_end */
TCGv    cpu_npc;

/* Macros */

#include "exec/gen-icount.h"
#define REG(x)  (cpu_r[x])

/* macro used to fix middle-endianess. */
#define ARRANGE_ENDIAN(endianess, buf)                  \
    ((endianess) ? arc_getm32(buf) : bswap32(buf))

/*
 * The macro to add boiler plate code for conditional execution.
 * It will add tcg_gen codes only if there is a condition to
 * be checked (ctx->insn.cc != 0). This macro assumes that there
 * is a "ctx" variable of type "DisasCtxt *" in context. Remember
 * to pair it with CC_EPILOGUE macro.
 */
#define CC_PROLOGUE                                   \
  TCGv cc = tcg_temp_local_new();                     \
  TCGLabel *done = gen_new_label();                   \
  do {                                                \
    if (ctx->insn.cc) {                               \
        arc_gen_verifyCCFlag(ctx, cc);                \
        tcg_gen_brcondi_tl(TCG_COND_NE, cc, 1, done); \
    }                                                 \
  } while (0)

/*
 * The finishing counter part of CC_PROLUGE. This is supposed
 * to be put at the end of the function using it.
 */
#define CC_EPILOGUE          \
    if (ctx->insn.cc) {      \
        gen_set_label(done); \
    }                        \
    tcg_temp_free(cc)

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
    if (unlikely(dc->base.singlestep_enabled)) {
        return false;
    }
#ifndef CONFIG_USER_ONLY
    return (dc->base.tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

void gen_goto_tb(DisasContext *ctx, int n, TCGv dest)
{
    tcg_gen_mov_tl(cpu_pc, dest);
    tcg_gen_andi_tl(cpu_pcl, dest, 0xfffffffc);
    if (ctx->base.singlestep_enabled) {
        gen_helper_debug(cpu_env);
    }
    tcg_gen_exit_tb(NULL, 0);
}

static void gen_gotoi_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_movi_tl(cpu_pcl, dest & 0xfffffffc);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_movi_tl(cpu_pcl, dest & 0xfffffffc);
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        }
        tcg_gen_exit_tb(NULL, 0);
    }
}

void arc_translate_init(void)
{
    int i;
    static int init_not_done = 1;

    if (init_not_done == 0) {
        return;
    }
#define ARC_REG_OFFS(x) offsetof(CPUARCState, x)

#define NEW_ARC_REG(x) \
        tcg_global_mem_new_i32(cpu_env, offsetof(CPUARCState, x), #x)

    cpu_S1f = NEW_ARC_REG(macmod.S1);
    cpu_S2f = NEW_ARC_REG(macmod.S2);
    cpu_CSf = NEW_ARC_REG(macmod.CS);

    cpu_Zf  = NEW_ARC_REG(stat.Zf);
    cpu_Lf  = NEW_ARC_REG(stat.Lf);
    cpu_Nf  = NEW_ARC_REG(stat.Nf);
    cpu_Cf  = NEW_ARC_REG(stat.Cf);
    cpu_Vf  = NEW_ARC_REG(stat.Vf);
    cpu_Uf  = NEW_ARC_REG(stat.Uf);
    cpu_DEf = NEW_ARC_REG(stat.DEf);
    cpu_ESf = NEW_ARC_REG(stat.ESf);
    cpu_AEf = NEW_ARC_REG(stat.AEf);
    cpu_Hf  = NEW_ARC_REG(stat.Hf);
    cpu_IEf = NEW_ARC_REG(stat.IEf);
    cpu_Ef  = NEW_ARC_REG(stat.Ef);

    cpu_is_delay_slot_instruction = NEW_ARC_REG(stat.is_delay_slot_instruction);

    cpu_l1_Zf = NEW_ARC_REG(stat_l1.Zf);
    cpu_l1_Lf = NEW_ARC_REG(stat_l1.Lf);
    cpu_l1_Nf = NEW_ARC_REG(stat_l1.Nf);
    cpu_l1_Cf = NEW_ARC_REG(stat_l1.Cf);
    cpu_l1_Vf = NEW_ARC_REG(stat_l1.Vf);
    cpu_l1_Uf = NEW_ARC_REG(stat_l1.Uf);
    cpu_l1_DEf = NEW_ARC_REG(stat_l1.DEf);
    cpu_l1_AEf = NEW_ARC_REG(stat_l1.AEf);
    cpu_l1_Hf = NEW_ARC_REG(stat_l1.Hf);

    cpu_er_Zf = NEW_ARC_REG(stat_er.Zf);
    cpu_er_Lf = NEW_ARC_REG(stat_er.Lf);
    cpu_er_Nf = NEW_ARC_REG(stat_er.Nf);
    cpu_er_Cf = NEW_ARC_REG(stat_er.Cf);
    cpu_er_Vf = NEW_ARC_REG(stat_er.Vf);
    cpu_er_Uf = NEW_ARC_REG(stat_er.Uf);
    cpu_er_DEf = NEW_ARC_REG(stat_er.DEf);
    cpu_er_AEf = NEW_ARC_REG(stat_er.AEf);
    cpu_er_Hf = NEW_ARC_REG(stat_er.Hf);

    cpu_eret = NEW_ARC_REG(eret);
    cpu_erbta = NEW_ARC_REG(erbta);
    cpu_ecr = NEW_ARC_REG(ecr);
    cpu_efa = NEW_ARC_REG(efa);
    cpu_bta = NEW_ARC_REG(bta);
    cpu_lps = NEW_ARC_REG(lps);
    cpu_lpe = NEW_ARC_REG(lpe);
    cpu_pc = NEW_ARC_REG(pc);
    cpu_npc = NEW_ARC_REG(npc);

    cpu_bta_l1 = NEW_ARC_REG(bta_l1);
    cpu_bta_l2 = NEW_ARC_REG(bta_l2);

    cpu_intvec = NEW_ARC_REG(intvec);

    for (i = 0; i < 64; i++) {
        char name[16];

        sprintf(name, "r[%d]", i);

        cpu_r[i] = tcg_global_mem_new_i32(cpu_env,
                                          ARC_REG_OFFS(r[i]),
                                          strdup(name));
    }

    cpu_gp     = cpu_r[26];
    cpu_fp     = cpu_r[27];
    cpu_sp     = cpu_r[28];
    cpu_ilink1 = cpu_r[29];
    cpu_ilink2 = cpu_r[30];
    cpu_blink  = cpu_r[31];
    cpu_acclo  = cpu_r[58];
    cpu_acchi  = cpu_r[59];
    cpu_lpc    = cpu_r[60];
    cpu_limm   = cpu_r[62];
    cpu_pcl    = cpu_r[63];

    cpu_debug_LD = NEW_ARC_REG(debug.LD);
    cpu_debug_SH = NEW_ARC_REG(debug.SH);
    cpu_debug_BH = NEW_ARC_REG(debug.BH);
    cpu_debug_UB = NEW_ARC_REG(debug.UB);
    cpu_debug_ZZ = NEW_ARC_REG(debug.ZZ);
    cpu_debug_RA = NEW_ARC_REG(debug.RA);
    cpu_debug_IS = NEW_ARC_REG(debug.IS);
    cpu_debug_FH = NEW_ARC_REG(debug.FH);
    cpu_debug_SS = NEW_ARC_REG(debug.SS);

    cpu_lock_lf_var = NEW_ARC_REG(lock_lf_var);

    init_not_done = 0;
}

static void arc_tr_init_disas_context(DisasContextBase *dcbase,
                                      CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    dc->base.is_jmp = DISAS_NEXT;
    dc->mem_idx = dc->base.tb->flags & 1;
}
static void arc_tr_tb_start(DisasContextBase *dcbase, CPUState *cpu)
{
    /* place holder for now */
}

static void arc_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);


    tcg_gen_insn_start(dc->base.pc_next);
    dc->cpc = dc->base.pc_next;

    if (dc->base.num_insns == dc->base.max_insns &&
        (dc->base.tb->cflags & CF_LAST_IO)) {
        gen_io_start();
    }
}

static bool arc_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                    const CPUBreakpoint *bp)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_movi_tl(cpu_pc, dc->cpc);
    dc->base.is_jmp = DISAS_NORETURN;
    gen_helper_debug(cpu_env);
    dc->base.pc_next += 2;
    return true;
}

static int arc_gen_INVALID(const DisasContext *ctx)
{
    fprintf(stderr, "invalid inst @:%08x\n", ctx->cpc);
    return DISAS_NEXT;
}

extern bool enabled_interrupts;

/* Arrange to middle endian, used by LITTLE ENDIAN systems. */
static uint32_t arc_getm32(uint32_t data)
{
    uint32_t value = 0;

    value  = (data & 0x0000ffff) << 16;
    value |= (data & 0xffff0000) >> 16;
    return value;
}

/*
 * Giving a CTX, decode it into an valid OPCODE_P if it
 * exists. Returns TRUE if successfully.
 */
static bool read_and_decode_context(DisasContext *ctx,
                                    const struct arc_opcode **opcode_p)
{
    uint16_t buffer[2];
    uint8_t length;
    uint64_t insn;

    /* Read the first 16 bits, figure it out what kind of instruction it is. */
    buffer[0] = cpu_lduw_code(ctx->env, ctx->cpc);
    length = arc_insn_length(buffer[0], ctx->env->family);

    switch (length) {
    case 2:
        /* 16-bit instructions. */
        insn = (uint64_t) buffer[0];
        break;
    case 4:
        /* 32-bit instructions. */
        buffer[1] = cpu_lduw_code(ctx->env, ctx->cpc + 2);
        uint32_t buf = (buffer[0] << 16) | buffer[1];
        insn = buf;
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * Now, we have read the entire opcode, decode it and place the
     * relevant info into opcode and ctx->insn.
     */
    *opcode_p = arc_find_format(&ctx->insn, insn, length, ctx->env->family);

    if (*opcode_p == NULL) {
        return false;
    }

    /*
     * If the instruction requires long immediate, read the extra 4
     * bytes and initialize the relevant fields.
     */
    if (ctx->insn.limm_p) {
        ctx->insn.limm = ARRANGE_ENDIAN(true,
                                        cpu_ldl_code(ctx->env,
                                        ctx->cpc + length));
        length += 4;
    } else {
        ctx->insn.limm = 0;
    }

    /* Update context. */
    ctx->insn.len = length;
    ctx->npc = ctx->cpc + length;
    ctx->pcl = ctx->cpc & 0xfffffffc;

    return true;
}

/* Check if OPR is a register _and_ an even numbered one. */
static inline bool is_odd_numbered_register(const operand_t opr)
{
   return (opr.type & ARC_OPERAND_IR) && (opr.value & 1);
}

/*
 * Going through every operand, if any of those is a register
 * it is verified to be an even numbered register. Else, an
 * exception is put in the generated code and FALSE is returned.
 */
static bool verify_all_regs_are_even(const DisasCtxt *ctx)
{
    for (int nop = 0; nop < ctx->insn.n_ops; ++nop) {
        if (is_odd_numbered_register(ctx->insn.operands[nop])) {
            arc_gen_excp(ctx, EXCP_INST_ERROR, 0, 0);
            return false;
        }
    }
    return true;
}

/*
 * Verifies if the destination operand (operand 0) is a register
 * then it is an even numbered one. Else, an exception is put in
 * the generated code and FALSE is returned.
 */
static bool verify_dest_reg_is_even(const DisasCtxt *ctx)
{
    if (is_odd_numbered_register(ctx->insn.operands[0])) {
        arc_gen_excp(ctx, EXCP_INST_ERROR, 0, 0);
        return false;
    }
    return true;
}

enum arc_opcode_map {
    MAP_NONE = -1,
#define SEMANTIC_FUNCTION(...)
#define CONSTANT(...)
#define MAPPING(MNEMONIC, NAME, NOPS, ...) MAP_##MNEMONIC##_##NAME,
#include "target/arc/semfunc_mapping.def"
#include "target/arc/extra_mapping.def"
#undef MAPPING
#undef CONSTANT
#undef SEMANTIC_FUNCTION
    /* Add some include to generated files */
    MAP_LAST
};

const char number_of_ops_semfunc[MAP_LAST + 1] = {
#define SEMANTIC_FUNCTION(...)
#define CONSTANT(...)
#define MAPPING(MNEMONIC, NAME, NOPS, ...) NOPS,
#include "target/arc/semfunc_mapping.def"
#include "target/arc/extra_mapping.def"
#undef MAPPING
#undef CONSTANT
#undef SEMANTIC_FUNCTION
    2
};

static enum arc_opcode_map arc_map_opcode(const struct arc_opcode *opcode)
{
#define SEMANTIC_FUNCTION(...)
#define CONSTANT(...)
#define MAPPING(MNEMONIC, NAME, ...)         \
    if (strcmp(opcode->name, #MNEMONIC) == 0) \
        return MAP_##MNEMONIC##_##NAME;
#include "target/arc/semfunc_mapping.def"
#include "target/arc/extra_mapping.def"
#undef MAPPING
#undef CONSTANT
#undef SEMANTIC_FUNCTION

    return MAP_NONE;
}

/* Code support for constant values coming from semantic function mapping. */
struct constant_operands {
    uint8_t operand_number;
    uint32_t default_value;
    struct constant_operands *next;
};

struct constant_operands *map_constant_operands[MAP_LAST];

static void add_constant_operand(enum arc_opcode_map mapping,
                                 uint8_t operand_number,
                                 uint32_t value)
{
    struct constant_operands **t = &(map_constant_operands[mapping]);
    while (*t != NULL) {
        t = &((*t)->next);
    }
    *t = (struct constant_operands *) malloc(sizeof(struct constant_operands));

    (*t)->operand_number = operand_number;
    (*t)->default_value = value;
    (*t)->next = NULL;
}

static struct constant_operands *
constant_entry_for(enum arc_opcode_map mapping,
                   uint8_t operand_number)
{
    struct constant_operands *t = map_constant_operands[mapping];
    while (t != NULL) {
        if (t->operand_number == operand_number) {
            return t;
        }
        t = t->next;
    }
    return NULL;
}

static void init_constants(void)
{
#define SEMANTIC_FUNCTION(...)
#define MAPPING(...)
#define CONSTANT(NAME, MNEMONIC, OP_NUM, VALUE) \
  add_constant_operand(MAP_##MNEMONIC##_##NAME, OP_NUM, VALUE);
#include "target/arc/semfunc_mapping.def"
#include "target/arc/extra_mapping.def"
#undef MAPPING
#undef CONSTANT
#undef SEMANTIC_FUNCTION
}

static void arc_debug_opcode(const struct arc_opcode *opcode,
                             DisasContext *ctx,
                             const char *msg)
{
    qemu_log_mask(LOG_UNIMP,
                  "%s for %s at pc=0x%08x\n",
                  msg, opcode->name, ctx->cpc);
}

static TCGv arc_decode_operand(const struct arc_opcode *opcode,
                               DisasContext *ctx,
                               unsigned char nop,
                               enum arc_opcode_map mapping)
{
    TCGv ret;

    if (nop >= ctx->insn.n_ops) {
        struct constant_operands *co = constant_entry_for(mapping, nop);
        assert(co != NULL);
        ret = tcg_const_local_i32(co->default_value);
        return ret;
    } else {
        operand_t operand = ctx->insn.operands[nop];

        if (operand.type & ARC_OPERAND_IR) {
            ret = cpu_r[operand.value];
            if (operand.value == 63) {
                tcg_gen_movi_tl(cpu_pcl, ctx->pcl);
            }
      } else {
            int32_t limm = operand.value;
            if (operand.type & ARC_OPERAND_LIMM) {
                limm = ctx->insn.limm;
                tcg_gen_movi_tl(cpu_limm, limm);
                ret = cpu_r[62];
            } else {
                ret = tcg_const_local_i32(limm);
            }
        }
    }

  return ret;
}

/* See translate.h. */
void arc_gen_excp(const DisasCtxt *ctx,
                  uint32_t index,
                  uint32_t causecode,
                  uint32_t param)
{
    TCGv_i32 tcg_index = tcg_const_i32(index);
    TCGv_i32 tcg_cause = tcg_const_i32(causecode);
    TCGv_i32 tcg_param = tcg_const_i32(param);

    tcg_gen_movi_tl(cpu_pc, ctx->cpc);
    tcg_gen_movi_tl(cpu_eret, ctx->cpc);
    tcg_gen_movi_tl(cpu_erbta, ctx->npc);

    gen_helper_raise_exception(cpu_env, tcg_index, tcg_cause, tcg_param);

    tcg_temp_free_i32(tcg_index);
    tcg_temp_free_i32(tcg_cause);
    tcg_temp_free_i32(tcg_param);
}

/* Generate trap. */
static void gen_trap(DisasContext *ctx, uint32_t param)
{
    TCGv_i32 tmp0 = tcg_const_i32(EXCP_TRAP);
    TCGv_i32 tmp1 = tcg_const_i32(0);
    TCGv_i32 tmp2 = tcg_const_i32(param);

    tcg_gen_movi_tl(cpu_pc, ctx->cpc);
    tcg_gen_movi_tl(cpu_eret, ctx->npc);
    tcg_gen_mov_tl(cpu_erbta, cpu_bta);

    gen_helper_raise_exception(cpu_env, tmp0, tmp1, tmp2);

    tcg_temp_free_i32(tmp0);
    tcg_temp_free_i32(tmp1);
    tcg_temp_free_i32(tmp2);
}

/* Generate sleep insn. */
static void gen_sleep(DisasContext *ctx, TCGv opa)
{
    uint32_t param = 0;

    if (ctx->insn.operands[0].type & ARC_OPERAND_IR) {
        TCGv tmp3 = tcg_temp_local_new_i32();
        TCGLabel *done_L = gen_new_label();

        tcg_gen_andi_tl(tmp3, opa, 0x10);
        tcg_gen_brcondi_tl(TCG_COND_NE, tmp3, 0x10, done_L);
        tcg_gen_andi_tl(cpu_Ef, opa, 0x0f);
        tcg_gen_movi_tl(cpu_IEf, 1);
        gen_set_label(done_L);

        tcg_temp_free_i32(tmp3);
    } else {
        param = ctx->insn.operands[0].value;
        if (param & 0x10) {
            tcg_gen_movi_tl(cpu_IEf, 1);
            tcg_gen_movi_tl(cpu_Ef, param & 0x0f);
        }
    }
    /* FIXME: setup debug registers as well. */

    TCGv npc = tcg_temp_local_new_i32();
    tcg_gen_movi_tl(npc, ctx->npc);
    gen_helper_halt(cpu_env, npc);
    tcg_temp_free_i32(npc);
    qemu_log_mask(CPU_LOG_TB_IN_ASM,
                  "CPU in sleep mode, waiting for an IRQ.\n");
}

/* Return from exception. */
static void gen_rtie(DisasContext *ctx)
{
    tcg_gen_movi_tl(cpu_pc, ctx->cpc);
    gen_helper_rtie(cpu_env);
    tcg_gen_mov_tl(cpu_pc, cpu_pcl);
    gen_goto_tb(ctx, 1, cpu_pc);
}

/* accumulator = b32 * c32 (signed multiplication). */
void
arc_gen_mpyd(const DisasCtxt *ctx, TCGv_i32 dest,
              TCGv_i32 b32, TCGv_i32 c32)
{
    CC_PROLOGUE;
    tcg_gen_muls2_i32(cpu_acclo, cpu_acchi, b32, c32);
    if (ctx->insn.operands[0].type & ARC_OPERAND_IR) {
        tcg_gen_mov_tl(arc_gen_next_reg(ctx, dest), cpu_acchi);
        tcg_gen_mov_tl(dest, cpu_acclo);
    }
    if (ctx->insn.f) {
        setNFlag(cpu_acchi);
        tcg_gen_movi_tl(cpu_Vf, 0);
    }
    CC_EPILOGUE;
}

/* accumulator = b32 * c32 (unsigned multiplication). */
void
arc_gen_mpydu(const DisasCtxt *ctx, TCGv_i32 dest,
               TCGv_i32 b32, TCGv_i32 c32)
{
    CC_PROLOGUE;
    tcg_gen_mulu2_i32(cpu_acclo, cpu_acchi, b32, c32);
    if (ctx->insn.operands[0].type & ARC_OPERAND_IR) {
        tcg_gen_mov_tl(arc_gen_next_reg(ctx, dest), cpu_acchi);
        tcg_gen_mov_tl(dest, cpu_acclo);
    }
    if (ctx->insn.f) {
        tcg_gen_movi_tl(cpu_Vf, 0);
    }
    CC_EPILOGUE;
}

/*
 * Populates a 64-bit vector with register pair:
 *   vec64=(REGn+1,REGn)=(REGn+1_hi,REGn+1_lo,REGn_hi,REGn_lo)
 * REG must be refering to an even numbered register.
 * Do not forget to free the returned TCGv_i64 when done!
 */
static TCGv_i64 pair_reg_to_i64(const DisasCtxt *ctx, TCGv_i32 reg)
{
    TCGv_i64 vec64 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(vec64, reg, arc_gen_next_reg(ctx, reg));
    return vec64;
}

/*
 * Populates a 32-bit vector with repeating SHIMM:
 *   vec32=(0000000000u6,0000000000u6)
 *   vec32=(sssss12,sssss12)
 * It's crucial that the s12 part of an encoding is in signed
 * integer form while passed along in SHIMM, e.g:
 *   s12 = -125 (0xf803) --> 0xfffff803
 * Do not forget to free the returned TCGv_i32 when done!
 */
static TCGv_i32 dup_shimm_to_i32(int16_t shimm)
{
    TCGv_i32 vec32 = tcg_temp_new_i32();
    int32_t val = shimm;
    val = ((val << 16) & 0xffff0000) | (val & 0xffff);
    tcg_gen_movi_i32(vec32, val);
    return vec32;
}

/*
 * Populates a 64-bit vector with repeating LIMM:
 *   vec64=(limm,limm)=(limm_hi,limm_lo,limm_hi,limm_lo)
 * Do not forget to free the returned TCGv_i64 when done!
 */
static TCGv_i64 dup_limm_to_i64(int32_t limm)
{
    TCGv_i64 vec64 = tcg_temp_new_i64();
    int64_t val = limm;
    val = (val << 32) | (val & 0xffffffff);
    tcg_gen_movi_i64(vec64, val);
    return vec64;
}

/*
 * Populates a 64-bit vector with four SHIMM (u6 or s12):
 *   vec64=(0000000000u6,0000000000u6,0000000000u6,0000000000u6)
 *   vec64=(sssss12,sssss12,sssss12,sssss12)
 * It's crucial that the s12 part of an encoding is in signed
 * integer form while passed along in SHIMM, e.g:
 *   s12 = -125 (0xf803) --> 0xfffff803
 * Do not forget to free the returned TCGv_i64 when done!
 */
static TCGv_i64 quad_shimm_to_i64(int16_t shimm)
{
    TCGv_i64 vec64 = tcg_temp_new_i64();
    int64_t val = shimm;
    val = (val << 48) | ((val << 32) & 0x0000ffff00000000) |
          ((val << 16) & 0x00000000ffff0000) | (val & 0xffff);
    tcg_gen_movi_i64(vec64, val);
    return vec64;
}

/*
 * gen_vec_op2 emits instructions to perform the desired operation,
 * defined by OP, on the inputs (B32 and C32) and returns the
 * result in DEST.
 *
 * vector size:     64-bit
 * vector elements: 2
 * element size:    32-bit
 *
 * (A1, A0) = (B1, B0) op (C1, C0)
 */
static void gen_vec_op2(const DisasCtxt *ctx,
                        void (*OP)(TCGv_i64, TCGv_i64, TCGv_i64),
                        TCGv_i32 dest,
                        TCGv_i32 b32,
                        TCGv_i32 c32)
{
    TCGv_i64 d64, b64, c64;

    /* If no real register for result, then this a nop. Bail out! */
    if (!(ctx->insn.operands[0].type & ARC_OPERAND_IR)) {
        return;
    }

    /* Extend B32 to B64 based on its type: {reg, limm}. */
    if (ctx->insn.operands[1].type & ARC_OPERAND_IR) {
        b64 = pair_reg_to_i64(ctx, b32);
    } else if (ctx->insn.operands[1].type & ARC_OPERAND_LIMM) {
        b64 = dup_limm_to_i64(ctx->insn.limm);
    } else {
        g_assert_not_reached();
    }
    /* Extend C32 to C64 based on its type: {reg, limm, shimm}. */
    if (ctx->insn.operands[2].type & ARC_OPERAND_IR) {
        c64 = pair_reg_to_i64(ctx, c32);
    } else if (ctx->insn.operands[2].type & ARC_OPERAND_LIMM) {
        c64 = dup_limm_to_i64(ctx->insn.limm);
    } else if (ctx->insn.operands[2].type & ARC_OPERAND_SHIMM) {
        /* At this point SHIMM is extended like LIMM. */
        c64 = dup_limm_to_i64(ctx->insn.operands[2].value);
    } else {
        g_assert_not_reached();
    }
    d64 = tcg_temp_new_i64();

    (*OP)(d64, b64, c64);
    tcg_gen_extrl_i64_i32(dest, d64);
    tcg_gen_extrh_i64_i32(arc_gen_next_reg(ctx, dest), d64);

    tcg_temp_free_i64(d64);
    tcg_temp_free_i64(c64);
    tcg_temp_free_i64(b64);
    return;
}

/*
 * gen_vec_op2h emits instructions to perform the desired operation,
 * defined by OP, on the inputs (B32 and C32) and returns the
 * result in DEST.
 *
 * vector size:     32-bit
 * vector elements: 2
 * element size:    16-bit
 *
 * (a1, a0) = (b1, b0) op (c1, c0)
 */
static void gen_vec_op2h(const DisasCtxt *ctx,
                         void (*OP)(TCGv, TCGv, TCGv),
                         TCGv_i32 dest,
                         TCGv_i32 b32,
                         TCGv_i32 c32)
{
    TCGv_i32 t0, t1;

    /* If no real register for result, then this a nop. Bail out! */
    if (!(ctx->insn.operands[0].type & ARC_OPERAND_IR)) {
        return;
    }

    t0 = tcg_temp_new();
    tcg_gen_mov_i32(t0, b32);
    /*
     * If the last operand is a u6/s12, say 63, there is no "HI" in it.
     * Instead, it must be duplicated to form a pair; e.g.: (63, 63).
     */
    if (ctx->insn.operands[2].type & ARC_OPERAND_SHIMM) {
        t1 = dup_shimm_to_i32(ctx->insn.operands[2].value);
    } else {
        t1 = tcg_temp_new();
        tcg_gen_mov_i32(t1, c32);
    }

    (*OP)(dest, t0, t1);

    tcg_temp_free(t1);
    tcg_temp_free(t0);
}


/*
 * gen_vec_op4h emits instructions to perform the desired operation,
 * defined by OP, on the inputs (B32 and C32) and returns the
 * result in DEST.
 *
 * vector size:     64-bit
 * vector elements: 4
 * element size:    16-bit
 *
 * (a3, a2, a1, a0) = (b3, b2, b1, b0) op (c3, c2, c1, c0)
 */
static void gen_vec_op4h(const DisasCtxt *ctx,
                         void (*op)(TCGv_i64, TCGv_i64, TCGv_i64),
                         TCGv_i32 dest,
                         TCGv_i32 b32,
                         TCGv_i32 c32)
{
    TCGv_i64 d64, b64, c64;

    /* If no real register for result, then this a nop. Bail out! */
    if (!(ctx->insn.operands[0].type & ARC_OPERAND_IR)) {
        return;
    }

    /* Extend B32 to B64 based on its type: {reg, limm}. */
    if (ctx->insn.operands[1].type & ARC_OPERAND_IR) {
        b64 = pair_reg_to_i64(ctx, b32);
    } else if (ctx->insn.operands[1].type & ARC_OPERAND_LIMM) {
        b64 = dup_limm_to_i64(ctx->insn.limm);
    } else {
        g_assert_not_reached();
    }
    /* Extend C32 to C64 based on its type: {reg, limm, shimm}. */
    if (ctx->insn.operands[2].type & ARC_OPERAND_IR) {
        c64 = pair_reg_to_i64(ctx, c32);
    } else if (ctx->insn.operands[2].type & ARC_OPERAND_LIMM) {
        c64 = dup_limm_to_i64(ctx->insn.limm);
    } else if (ctx->insn.operands[2].type & ARC_OPERAND_SHIMM) {
        c64 = quad_shimm_to_i64(ctx->insn.operands[2].value);
    } else {
        g_assert_not_reached();
    }
    d64 = tcg_temp_new_i64();

    (*op)(d64, b64, c64);
    tcg_gen_extrl_i64_i32(dest, d64);
    tcg_gen_extrh_i64_i32(arc_gen_next_reg(ctx, dest), d64);

    tcg_temp_free_i64(d64);
    tcg_temp_free_i64(c64);
    tcg_temp_free_i64(b64);
    return;
}

/*
 * To use a 32-bit adder to sum two 16-bit numbers:
 * 1) Mask out the 16th bit in both operands to cause no carry.
 * 2) Add the numbers.
 * 3) Put back the 16th bit sum: T0[15] ^ T1[15] ^ CARRY[14]
 *    (ignoring the possible carry generated)
 * T0 and T1 values will change. Use temporary ones.
 */
static void gen_add16(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_xor_i32(tmp, t0, t1);
    tcg_gen_andi_i32(tmp, tmp, 0x8000);
    tcg_gen_andi_i32(t0, t0, ~0x8000);
    tcg_gen_andi_i32(t1, t1, ~0x8000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_xor_i32(dest, t0, tmp);
    tcg_temp_free_i32(tmp);
}

/*
 * To use a 32-bit subtracter to subtract two 16-bit numbers:
 * 0) Record how T0[15]-T1[15] would result without other bits.
 * 1) Make the 16th bit for the first operand 1 and the second
 *    operand 0. This combination of (1 - 0) will absorb any
 *    possible borrow that may come from the 15th bit.
 * 2) Subtract the numbers.
 * 3) Correct the 16th bit result (1 - 0 - B):
 *    If the 16th bit is 1 --> no borrow was asked.
 *    If the 16th bit is 0 --> a  borrow was asked.
 *    and if a borrow was asked, the result of step 0 must be
 *    inverted (0 -> 1 and 1 -> 0). If not, the result of step
 *    0 can be used readily:
 *     STEP2[15] | T0[15]-T1[15] | DEST[15]
 *     ----------+---------------+---------
 *         0     |       0       |    1
 *         0     |       1       |    0
 *         1     |       0       |    0
 *         1     |       1       |    1
 *    This is a truth table for XNOR(a,b):
 *      NOT(XOR(a,b))=XOR(XOR(a,b),1)
 * This approach might seem pedantic, but it generates one less
 * instruction than the obvious mask-and-sub approach and requires
 * two less TCG variables.
 * T0 and T1 values will change. Use temporary ones.
 */
static void gen_sub16(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_xor_i32(tmp, t0, t1);          /* step 0 */
    tcg_gen_andi_i32(tmp, tmp, 0x8000);    /* step 0 */
    tcg_gen_ori_i32(t0, t0, 0x8000);       /* step 1 */
    tcg_gen_andi_i32(t1, t1, ~0x8000);     /* step 1 */
    tcg_gen_sub_i32(t0, t0, t1);           /* step 2 */
    tcg_gen_xor_i32(dest, t0, tmp);        /* step 3 */
    tcg_gen_xori_i32(dest, dest, 0x8000);  /* step 3 */
    tcg_temp_free_i32(tmp);
}

void
arc_gen_vadd2(const DisasCtxt *ctx, TCGv dest, TCGv_i32 b, TCGv_i32 c)
{
    CC_PROLOGUE;
    gen_vec_op2(ctx, tcg_gen_vec_add32_i64, dest, b, c);
    CC_EPILOGUE;
}

void
arc_gen_vadd2h(const DisasCtxt *ctx, TCGv dest, TCGv_i32 b, TCGv_i32 c)
{
    CC_PROLOGUE;
    gen_vec_op2h(ctx, gen_add16, dest, b, c);
    CC_EPILOGUE;
}

void
arc_gen_vadd4h(const DisasCtxt *ctx, TCGv dest, TCGv_i32 b, TCGv_i32 c)
{
    CC_PROLOGUE;
    gen_vec_op4h(ctx, tcg_gen_vec_add16_i64, dest, b, c);
    CC_EPILOGUE;
}

void
arc_gen_vsub2(const DisasCtxt *ctx, TCGv dest, TCGv_i32 b, TCGv_i32 c)
{
    CC_PROLOGUE;
    gen_vec_op2(ctx, tcg_gen_vec_sub32_i64, dest, b, c);
    CC_EPILOGUE;
}

void
arc_gen_vsub2h(const DisasCtxt *ctx, TCGv dest, TCGv_i32 b, TCGv_i32 c)
{
    CC_PROLOGUE;
    gen_vec_op2h(ctx, gen_sub16, dest, b, c);
    CC_EPILOGUE;
}

void
arc_gen_vsub4h(const DisasCtxt *ctx, TCGv dest, TCGv_i32 b, TCGv_i32 c)
{
    CC_PROLOGUE;
    gen_vec_op4h(ctx, tcg_gen_vec_sub16_i64, dest, b, c);
    CC_EPILOGUE;
}


/* Given a CTX, generate the relevant TCG code for the given opcode. */
static int arc_decode(DisasContext *ctx, const struct arc_opcode *opcode)
{
    int ret = DISAS_NEXT;
    enum arc_opcode_map mapping;
    static bool initialized = false;

    if (initialized == false) {
        init_constants();
        initialized = true;
    }

    /* Do the mapping. */
    mapping = arc_map_opcode(opcode);
    if (mapping != MAP_NONE) {
        TCGv ops[10];
        int i;
        for (i = 0; i < number_of_ops_semfunc[mapping]; i++) {
            ops[i] = arc_decode_operand(opcode, ctx, i, mapping);
        }

        /*
         * Store some elements statically to implement less dynamic
         * features of instructions.  Started by the need to keep a
         * static reference to LP_START and LP_END.
         */

#define SEMANTIC_FUNCTION_CALL_0(NAME, A)       \
            arc_gen_##NAME(ctx);
#define SEMANTIC_FUNCTION_CALL_1(NAME, A)       \
            arc_gen_##NAME(ctx, ops[A]);
#define SEMANTIC_FUNCTION_CALL_2(NAME, A, B)            \
            arc_gen_##NAME(ctx, ops[A], ops[B]);
#define SEMANTIC_FUNCTION_CALL_3(NAME, A, B, C)                 \
            arc_gen_##NAME(ctx, ops[A], ops[B], ops[C]);
#define SEMANTIC_FUNCTION_CALL_4(NAME, A, B, C, D)                      \
            arc_gen_##NAME(ctx, ops[A], ops[B], ops[C], ops[D]);

#define SEMANTIC_FUNCTION(...)
#define CONSTANT(...)
#define MAPPING(MNEMONIC, NAME, NOPS, ...)                              \
            case MAP_##MNEMONIC##_##NAME:                               \
                ret = SEMANTIC_FUNCTION_CALL_##NOPS(NAME, __VA_ARGS__); \
                break;
        switch (mapping) {
#include "target/arc/semfunc_mapping.def"

        case MAP_swi_SWI:
        case MAP_swi_s_SWI:
            arc_gen_excp(ctx, EXCP_SWI, 0, ctx->insn.operands[0].value);
            ret = DISAS_NEXT;
            break;

        case MAP_trap_s_TRAP:
            gen_trap(ctx, ctx->insn.operands[0].value);
            ret = DISAS_NORETURN;
            break;

        case MAP_rtie_RTIE:
            gen_rtie(ctx);
            ret = DISAS_NORETURN;
            break;

        case MAP_sleep_SLEEP:
            gen_sleep(ctx, ops[0]);
            ret = DISAS_NEXT;
            break;

        case MAP_vadd2_VADD:
            if (verify_all_regs_are_even(ctx)) {
                arc_gen_vadd2(ctx, ops[0], ops[1], ops[2]);
            }
            ret = DISAS_NEXT;
            break;
        case MAP_vadd2h_VADD:
            arc_gen_vadd2h(ctx, ops[0], ops[1], ops[2]);
            ret = DISAS_NEXT;
            break;
        case MAP_vadd4h_VADD:
            if (verify_all_regs_are_even(ctx)) {
                arc_gen_vadd4h(ctx, ops[0], ops[1], ops[2]);
            }
            ret = DISAS_NEXT;
            break;

        case MAP_vsub2_VSUB:
            if (verify_all_regs_are_even(ctx)) {
                arc_gen_vsub2(ctx, ops[0], ops[1], ops[2]);
            }
            ret = DISAS_NEXT;
            break;
        case MAP_vsub2h_VSUB:
            arc_gen_vsub2h(ctx, ops[0], ops[1], ops[2]);
            ret = DISAS_NEXT;
            break;
        case MAP_vsub4h_VSUB:
            if (verify_all_regs_are_even(ctx)) {
                arc_gen_vsub4h(ctx, ops[0], ops[1], ops[2]);
            }
            ret = DISAS_NEXT;
            break;

        case MAP_mpyd_MPYD:
            if (verify_dest_reg_is_even(ctx)) {
                arc_gen_mpyd(ctx, ops[0], ops[1], ops[2]);
            }
            ret = DISAS_NEXT;
            break;
        case MAP_mpydu_MPYD:
            if (verify_dest_reg_is_even(ctx)) {
                arc_gen_mpydu(ctx, ops[0], ops[1], ops[2]);
            }
            ret = DISAS_NEXT;
            break;

        default:
            arc_debug_opcode(opcode, ctx, "No handle for map opcode");
            g_assert(!"Semantic not handled: Use -d unimp to list it.");
        }
#undef MAPPING
#undef CONSTANT
#undef SEMANTIC_FUNCTION
#undef SEMANTIC_FUNCTION_CALL_0
#undef SEMANTIC_FUNCTION_CALL_1
#undef SEMANTIC_FUNCTION_CALL_2
#undef SEMANTIC_FUNCTION_CALL_3

        for (i = 0; i < number_of_ops_semfunc[mapping]; i++) {
            operand_t operand = ctx->insn.operands[i];
            if (!(operand.type & ARC_OPERAND_LIMM) &&
                !(operand.type & ARC_OPERAND_IR)) {
                tcg_temp_free_i32(ops[i]);
            }
        }

    } else {
        arc_debug_opcode(opcode, ctx, "No mapping for opcode");
        g_assert(!"Semantic not found: Use -d unimp to list it.");
    }

    return ret;
}

void decode_opc(CPUARCState *env, DisasContext *ctx)
{
    ctx->env = env;

    enabled_interrupts = false;

    const struct arc_opcode *opcode = NULL;
    if (!read_and_decode_context(ctx, &opcode)) {
        ctx->base.is_jmp = arc_gen_INVALID(ctx);
        return;
    }

    ctx->base.is_jmp = arc_decode(ctx, opcode);

    TCGv npc = tcg_const_local_i32(ctx->npc);
    gen_helper_zol_verify(cpu_env, npc);
    tcg_temp_free(npc);

    enabled_interrupts = true;
}

static void arc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    bool in_a_delayslot_instruction = false;
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARCState *env = cpu->env_ptr;

    /* TODO (issue #62): these must be removed */
    dc->zero = tcg_const_local_i32(0);
    dc->one  = tcg_const_local_i32(1);

    if (env->stat.is_delay_slot_instruction == 1) {
        in_a_delayslot_instruction = true;
    }

    dc->cpc = dc->base.pc_next;
    decode_opc(env, dc);

    dc->base.pc_next = dc->npc;
    tcg_gen_movi_tl(cpu_npc, dc->npc);

    if (in_a_delayslot_instruction == true) {
        dc->base.is_jmp = DISAS_NORETURN;

        /* Post execution delayslot logic. */
        TCGLabel *DEf_not_set_label1 = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, cpu_DEf, 1, DEf_not_set_label1);
        tcg_gen_movi_tl(cpu_DEf, 0);
        gen_goto_tb(dc, 1, cpu_bta);
        gen_set_label(DEf_not_set_label1);
        env->stat.is_delay_slot_instruction = 0;
    }

    if (dc->base.is_jmp == DISAS_NORETURN) {
        gen_gotoi_tb(dc, 0, dc->npc);
    } else if (dc->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = dc->base.pc_first & TARGET_PAGE_MASK;
        if (dc->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            dc->base.is_jmp = DISAS_TOO_MANY;
        }
    }

    /* TODO (issue #62): these must be removed. */
    tcg_temp_free_i32(dc->zero);
    tcg_temp_free_i32(dc->one);

    /* verify if there is any TCG temporaries leakge */
    translator_loop_temp_check(dcbase);
}

static void arc_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_TOO_MANY:
    case DISAS_UPDATE:
        gen_gotoi_tb(dc, 0, dc->base.pc_next);
        break;
    case DISAS_BRANCH_IN_DELAYSLOT:
    case DISAS_NORETURN:
        break;
    default:
         g_assert_not_reached();
    }

    if (dc->base.num_insns == dc->base.max_insns &&
        (dc->base.tb->cflags & CF_LAST_IO)) {
        gen_io_end();
    }
}

static void arc_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    qemu_log("IN: %s\n", lookup_symbol(dc->base.pc_first));
    log_target_disas(cpu, dc->base.pc_first, dc->base.tb->size);
}


static const TranslatorOps arc_translator_ops = {
    .init_disas_context = arc_tr_init_disas_context,
    .tb_start           = arc_tr_tb_start,
    .insn_start         = arc_tr_insn_start,
    .breakpoint_check   = arc_tr_breakpoint_check,
    .translate_insn     = arc_tr_translate_insn,
    .tb_stop            = arc_tr_tb_stop,
    .disas_log          = arc_tr_disas_log,
};

/* generate intermediate code for basic block 'tb'. */
void gen_intermediate_code(CPUState *cpu,
                           TranslationBlock *tb,
                           int max_insns)
{
    DisasContext dc;
    const TranslatorOps *ops = &arc_translator_ops;
    translator_loop(ops, &dc.base, cpu, tb, max_insns);
}

void restore_state_to_opc(CPUARCState *env,
                          TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}

void arc_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    ARCCPU *cpu = ARC_CPU(cs);
    CPUARCState *env = &cpu->env;
    int i;

    qemu_fprintf(f, "STATUS:  [ %c %c %c %c %c %c %s %s %s %s %s %s %c]\n",
                        env->stat.Lf ? 'L' : '-',
                        env->stat.Zf ? 'Z' : '-',
                        env->stat.Nf ? 'N' : '-',
                        env->stat.Cf ? 'C' : '-',
                        env->stat.Vf ? 'V' : '-',
                        env->stat.Uf ? 'U' : '-',
                        env->stat.DEf ? "DE" : "--",
                        env->stat.AEf ? "AE" : "--",
                        env->stat.Ef ? "E" : "--",
                        env->stat.DZf ? "DZ" : "--",
                        env->stat.SCf ? "SC" : "--",
                        env->stat.IEf ? "IE" : "--",
                        env->stat.Hf ? 'H' : '-'
                        );

    qemu_fprintf(f, "\n");
    for (i = 0; i < ARRAY_SIZE(env->r); i++) {
        qemu_fprintf(f, "R[%02d]:  %02x   ", i, env->r[i]);

        if ((i % 8) == 7) {
            qemu_fprintf(f, "\n");
        }
    }
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
