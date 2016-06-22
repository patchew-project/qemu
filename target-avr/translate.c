/*
 *  QEMU AVR CPU
 *
 *  Copyright (c) 2016 Michael Rolnik
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"

typedef struct DisasContext DisasContext;
typedef struct InstInfo     InstInfo;

/*This is the state at translation time.  */
struct DisasContext {
    struct TranslationBlock    *tb;

    /*Routine used to access memory */
    int                         memidx;
    int                         bstate;
    int                         singlestep;
};

enum {
    BS_NONE = 0,    /*  Nothing special (none of the below          */
    BS_STOP = 1,    /*  We want to stop translation for any reason  */
    BS_BRANCH = 2,    /*  A branch condition is reached               */
    BS_EXCP = 3,    /*  An exception condition is reached           */
};

static TCGv_env cpu_env;

static TCGv     cpu_pc;

static TCGv     cpu_Cf;
static TCGv     cpu_Zf;
static TCGv     cpu_Nf;
static TCGv     cpu_Vf;
static TCGv     cpu_Sf;
static TCGv     cpu_Hf;
static TCGv     cpu_Tf;
static TCGv     cpu_If;

static TCGv     cpu_rampD;
static TCGv     cpu_rampX;
static TCGv     cpu_rampY;
static TCGv     cpu_rampZ;

static TCGv     cpu_io[64];
static TCGv     cpu_r[32];
static TCGv     cpu_eind;
static TCGv     cpu_sp;

#include "exec/gen-icount.h"
#define REG(x)  (cpu_r[x])

void avr_translate_init(void)
{
    int i;
    static int done_init;

    if (done_init) {
        return;
    }
#define AVR_REG_OFFS(x) offsetof(CPUAVRState, x)
    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_pc = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(pc_w),  "pc");
    cpu_Cf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregC), "Cf");
    cpu_Zf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregZ), "Zf");
    cpu_Nf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregN), "Nf");
    cpu_Vf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregV), "Vf");
    cpu_Sf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregS), "Sf");
    cpu_Hf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregH), "Hf");
    cpu_Tf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregT), "Tf");
    cpu_If = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregI), "If");
    cpu_rampD = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampD), "rampD");
    cpu_rampX = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampX), "rampX");
    cpu_rampY = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampY), "rampY");
    cpu_rampZ = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampZ), "rampZ");
    cpu_eind = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(eind),  "eind");
    cpu_sp = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sp),    "sp");

    for (i = 0; i < 64; i++) {
        char    name[16];

        sprintf(name, "io[%d]", i);

        cpu_io[i] = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(io[i]), name);
    }
    for (i = 0; i < 32; i++) {
        char    name[16];

        sprintf(name, "r[%d]", i);

        cpu_r[i] = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(r[i]), name);
    }

    done_init = 1;
}

static inline void gen_goto_tb(CPUAVRState *env, DisasContext *ctx, int n,
                                target_ulong dest)
{
    TranslationBlock   *tb;

    tb = ctx->tb;

    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)
        &&  (ctx->singlestep == 0)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);

        if (ctx->singlestep) {
            gen_helper_debug(cpu_env);
        }
        tcg_gen_exit_tb(0);
    }
}

/*generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUAVRState *env, struct TranslationBlock *tb)
{
    AVRCPU         *cpu = avr_env_get_cpu(env);
    CPUState       *cs = CPU(cpu);
    DisasContext    ctx;
    target_ulong    pc_start;
    int             num_insns,
                    max_insns;
    target_ulong    cpc;
    target_ulong    npc;

    pc_start = tb->pc / 2;
    ctx.tb = tb;
    ctx.memidx = 0;
    ctx.bstate = BS_NONE;
    ctx.singlestep = cs->singlestep_enabled;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;

    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    gen_tb_start(tb);

    /*  decode first instruction    */
    cpc = pc_start;
    npc = cpc + 1;
    do {
        /*  translate current instruction   */
        tcg_gen_insn_start(cpc);
        num_insns++;

        /*  just skip to next instruction   */
        cpc++;
        npc++;
        ctx.bstate = BS_NONE;

        if (unlikely(cpu_breakpoint_test(cs, cpc * 2, BP_ANY))) {
            tcg_gen_movi_i32(cpu_pc, cpc);
            gen_helper_debug(cpu_env);
            ctx.bstate = BS_EXCP;
            /*The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            goto done_generating;
        }

        if (num_insns >= max_insns) {
            break;      /* max translated instructions limit reached */
        }
        if (ctx.singlestep) {
            break;      /* single step */
        }
        if ((cpc & (TARGET_PAGE_SIZE - 1)) == 0) {
            break;      /* page boundary */
        }
    } while (ctx.bstate == BS_NONE && !tcg_op_buf_full());

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (ctx.singlestep) {
        if (ctx.bstate == BS_STOP || ctx.bstate == BS_NONE) {
            tcg_gen_movi_tl(cpu_pc, npc);
        }
        gen_helper_debug(cpu_env);
        tcg_gen_exit_tb(0);
    } else {
        switch (ctx.bstate) {
        case BS_STOP:
        case BS_NONE:
            gen_goto_tb(env, &ctx, 0, npc);
            break;
        case BS_EXCP:
            tcg_gen_exit_tb(0);
            break;
        default:
            break;
        }
    }

done_generating:
    env->fullwr = false;
    gen_tb_end(tb, num_insns);

    tb->size = (npc - pc_start) * 2;
    tb->icount = num_insns;
}

void restore_state_to_opc(CPUAVRState *env, TranslationBlock *tb,
                                target_ulong *data)
{
    env->pc_w = data[0];
}

void avr_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                                int flags)
{
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    cpu_fprintf(f, "\n");
    cpu_fprintf(f, "PC:    %06x\n", env->pc_w);
    cpu_fprintf(f, "SP:      %04x\n", env->sp);
    cpu_fprintf(f, "rampD:     %02x\n", env->rampD >> 16);
    cpu_fprintf(f, "rampX:     %02x\n", env->rampX >> 16);
    cpu_fprintf(f, "rampY:     %02x\n", env->rampY >> 16);
    cpu_fprintf(f, "rampZ:     %02x\n", env->rampZ >> 16);
    cpu_fprintf(f, "EIND:      %02x\n", env->eind);
    cpu_fprintf(f, "X:       %02x%02x\n", env->r[27], env->r[26]);
    cpu_fprintf(f, "Y:       %02x%02x\n", env->r[29], env->r[28]);
    cpu_fprintf(f, "Z:       %02x%02x\n", env->r[31], env->r[30]);
    cpu_fprintf(f, "SREG:    [ %c %c %c %c %c %c %c %c ]\n",
                        env->sregI ? 'I' : '-',
                        env->sregT ? 'T' : '-',
                        env->sregH ? 'H' : '-',
                        env->sregS ? 'S' : '-',
                        env->sregV ? 'V' : '-',
                        env->sregN ? '-' : 'N', /*  Zf has negative logic   */
                        env->sregZ ? 'Z' : '-',
                        env->sregC ? 'I' : '-');

    cpu_fprintf(f, "\n");
    for (int i = 0; i < ARRAY_SIZE(env->r); i++) {
        cpu_fprintf(f, "R[%02d]:  %02x   ", i, env->r[i]);

        if ((i % 8) == 7) {
            cpu_fprintf(f, "\n");
        }
    }

    cpu_fprintf(f, "\n");
    for (int i = 0; i < ARRAY_SIZE(env->io); i++) {
        cpu_fprintf(f, "IO[%02d]: %02x   ", i, env->io[i]);

        if ((i % 8) == 7) {
            cpu_fprintf(f, "\n");
        }
    }
}
