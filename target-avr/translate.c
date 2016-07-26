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

#include "translate.h"

TCGv_env cpu_env;

TCGv cpu_pc;

TCGv cpu_Cf;
TCGv cpu_Zf;
TCGv cpu_Nf;
TCGv cpu_Vf;
TCGv cpu_Sf;
TCGv cpu_Hf;
TCGv cpu_Tf;
TCGv cpu_If;

TCGv cpu_rampD;
TCGv cpu_rampX;
TCGv cpu_rampY;
TCGv cpu_rampZ;

TCGv cpu_io[64];
TCGv cpu_r[32];
TCGv cpu_eind;
TCGv cpu_sp;

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

static int translate_nop(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    return BS_NONE;
}

void avr_decode(uint32_t pc, uint32_t *length, uint32_t opcode,
                        translate_function_t *translate)
{
    *length = 32;
    *translate = &translate_nop;
}

static void decode_opc(AVRCPU *cpu, DisasContext *ctx, InstInfo *inst)
{
    CPUAVRState *env = &cpu->env;

    inst->opcode = cpu_ldl_code(env, inst->cpc * 2);/* pc points to words */
    inst->length = 16;
    inst->translate = NULL;

    /* the following function looks onto the opcode as a string of bytes */
    avr_decode(inst->cpc, &inst->length, inst->opcode, &inst->translate);

    if (inst->length == 16) {
        inst->npc = inst->cpc + 1;
        /* get opcode as 16bit value */
        inst->opcode = inst->opcode & 0x0000ffff;
    }
    if (inst->length == 32) {
        inst->npc = inst->cpc + 2;
        /* get opcode as 32bit value */
        inst->opcode = (inst->opcode << 16)
                     | (inst->opcode >> 16);
    }
}

/* generate intermediate code for basic block 'tb'. */
void gen_intermediate_code(CPUAVRState *env, struct TranslationBlock *tb)
{
    AVRCPU *cpu = avr_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    DisasContext ctx;
    target_ulong pc_start;
    int num_insns, max_insns;
    target_ulong cpc;
    target_ulong npc;

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
    if (tb->flags & TB_FLAGS_FULL_ACCESS) {
        /*
            this flag is set by ST/LD instruction
            we will regenerate ONLY it with mem/cpu memory access
            insttead of mem access
        */
        max_insns = 1;
    }

    gen_tb_start(tb);

    /* decode first instruction */
    ctx.inst[0].cpc = pc_start;
    decode_opc(cpu, &ctx, &ctx.inst[0]);
    do {
        /* set curr/next PCs */
        cpc = ctx.inst[0].cpc;
        npc = ctx.inst[0].npc;

        /* decode next instruction */
        ctx.inst[1].cpc = ctx.inst[0].npc;
        decode_opc(cpu, &ctx, &ctx.inst[1]);

        /* translate current instruction */
        tcg_gen_insn_start(cpc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, cpc * 2, BP_ANY))) {
            tcg_gen_movi_i32(cpu_pc, cpc);
            gen_helper_debug(cpu_env);
            ctx.bstate = BS_EXCP;
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing. */
            goto done_generating;
        }

        ctx.bstate = ctx.inst[0].translate(env, &ctx, ctx.inst[0].opcode);

        if (num_insns >= max_insns) {
            break; /* max translated instructions limit reached */
        }
        if (ctx.singlestep) {
            break; /* single step */
        }
        if ((cpc & (TARGET_PAGE_SIZE - 1)) == 0) {
            break; /* page boundary */
        }

        ctx.inst[0] = ctx.inst[1]; /* make next inst curr */
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
    int i;

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
                        env->sregN ? '-' : 'N', /* Zf has negative logic */
                        env->sregZ ? 'Z' : '-',
                        env->sregC ? 'I' : '-');

    cpu_fprintf(f, "\n");
    for (i = 0; i < ARRAY_SIZE(env->r); i++) {
        cpu_fprintf(f, "R[%02d]:  %02x   ", i, env->r[i]);

        if ((i % 8) == 7) {
            cpu_fprintf(f, "\n");
        }
    }

    cpu_fprintf(f, "\n");
    for (i = 0; i < ARRAY_SIZE(env->io); i++) {
        cpu_fprintf(f, "IO[%02d]: %02x   ", i, env->io[i]);

        if ((i % 8) == 7) {
            cpu_fprintf(f, "\n");
        }
    }
}

