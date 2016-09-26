/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Author: Sagar Karandikar, sagark@eecs.berkeley.edu
 *
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
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

static const char * const regnames[] = {
  "zero", "ra  ", "sp  ", "gp  ", "tp  ", "t0  ",  "t1  ",  "t2  ",
  "s0  ", "s1  ", "a0  ", "a1  ", "a2  ", "a3  ",  "a4  ",  "a5  ",
  "a6  ", "a7  ", "s2  ", "s3  ", "s4  ", "s5  ",  "s6  ",  "s7  ",
  "s8  ", "s9  ", "s10 ", "s11 ", "t3  ", "t4  ",  "t5  ",  "t6  "
};

static const char * const fpr_regnames[] = {
  "ft0", "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
  "fs0", "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
  "fa6", "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
  "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
};

void gen_intermediate_code(CPURISCVState *env, TranslationBlock *tb)
{
}

void riscv_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "pc=0x" TARGET_FMT_lx "\n", env->PC);
    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames[i], env->gpr[i]);
        if ((i & 3) == 3) {
            cpu_fprintf(f, "\n");
        }
    }

    cpu_fprintf(f, " %s " TARGET_FMT_lx "\n", "MSTATUS ",
                env->csr[CSR_MSTATUS]);
    cpu_fprintf(f, " %s " TARGET_FMT_lx "\n", "MIP     ", env->csr[CSR_MIP]);
    cpu_fprintf(f, " %s " TARGET_FMT_lx "\n", "MIE     ", env->csr[CSR_MIE]);

    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0) {
            cpu_fprintf(f, "FPR%02d:", i);
        }
        cpu_fprintf(f, " %s %016lx", fpr_regnames[i], env->fpr[i]);
        if ((i & 3) == 3) {
            cpu_fprintf(f, "\n");
        }
    }
}

void riscv_tcg_init(void)
{
}

#define MCPUID_RV64I   (2L << (TARGET_LONG_BITS - 2))
#define MCPUID_RV32I   (1L << (TARGET_LONG_BITS - 2))
#define MCPUID_SUPER   (1L << ('S' - 'A'))
#define MCPUID_USER    (1L << ('U' - 'A'))
#define MCPUID_I       (1L << ('I' - 'A'))
#define MCPUID_M       (1L << ('M' - 'A'))
#define MCPUID_A       (1L << ('A' - 'A'))
#define MCPUID_F       (1L << ('F' - 'A'))
#define MCPUID_D       (1L << ('D' - 'A'))

struct riscv_def_t {
    const char *name;
    uint64_t init_misa_reg;
};

/* RISC-V CPU definitions */
static const riscv_def_t riscv_defs[] = {
    {
        .name = "riscv",
#if defined(TARGET_RISCV64)
        /* RV64G */
        .init_misa_reg = MCPUID_RV64I | MCPUID_SUPER | MCPUID_USER | MCPUID_I
            | MCPUID_M | MCPUID_A | MCPUID_F | MCPUID_D,
#else
        /* RV32G */
        .init_misa_reg = MCPUID_RV32I | MCPUID_SUPER | MCPUID_USER | MCPUID_I
            | MCPUID_M | MCPUID_A | MCPUID_F | MCPUID_D,
#endif
    },
};

void riscv_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(riscv_defs); i++) {
        (*cpu_fprintf)(f, "RISCV '%s'\n", riscv_defs[i].name);
    }
}

RISCVCPU *cpu_riscv_init(const char *cpu_model)
{
    return NULL;
}

void restore_state_to_opc(CPURISCVState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->PC = data[0];
}
