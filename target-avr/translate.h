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

#ifndef AVR_TRANSLATE_H_
#define AVR_TRANSLATE_H_

#include "qemu/osdep.h"

#include "tcg/tcg.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "translate-inst.h"

extern TCGv_env cpu_env;

extern TCGv cpu_pc;

extern TCGv cpu_Cf;
extern TCGv cpu_Zf;
extern TCGv cpu_Nf;
extern TCGv cpu_Vf;
extern TCGv cpu_Sf;
extern TCGv cpu_Hf;
extern TCGv cpu_Tf;
extern TCGv cpu_If;

extern TCGv cpu_rampD;
extern TCGv cpu_rampX;
extern TCGv cpu_rampY;
extern TCGv cpu_rampZ;

extern TCGv cpu_io[64];
extern TCGv cpu_r[32];
extern TCGv cpu_eind;
extern TCGv cpu_sp;

enum {
    BS_NONE = 0, /* Nothing special (none of the below) */
    BS_STOP = 1, /* We want to stop translation for any reason */
    BS_BRANCH = 2, /* A branch condition is reached */
    BS_EXCP = 3, /* An exception condition is reached */
};

uint32_t    get_opcode(uint8_t const *code, unsigned bitBase, unsigned bitSize);

typedef struct DisasContext DisasContext;
typedef struct InstInfo InstInfo;

typedef int (*translate_function_t)(CPUAVRState *env, DisasContext *ctx,
                                        uint32_t opcode);
struct InstInfo {
    target_long cpc;
    target_long npc;
    uint32_t opcode;
    translate_function_t translate;
    unsigned length;
};

/* This is the state at translation time. */
struct DisasContext {
    struct TranslationBlock *tb;

    InstInfo inst[2];/* two consecutive instructions */

    /* Routine used to access memory */
    int memidx;
    int bstate;
    int singlestep;
};

void avr_decode(uint32_t pc, uint32_t *length, uint32_t opcode,
                        translate_function_t *translate);

static inline void  gen_goto_tb(CPUAVRState *env, DisasContext *ctx,
                        int n, target_ulong dest)
{
    TranslationBlock *tb;

    tb = ctx->tb;

    if (ctx->singlestep == 0) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        gen_helper_debug(cpu_env);
        tcg_gen_exit_tb(0);
    }
}

#endif

