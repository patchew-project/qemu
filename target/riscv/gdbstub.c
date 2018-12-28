/*
 * RISC-V GDB Server Stub
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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
#include "qemu-common.h"
#include "exec/gdbstub.h"
#include "cpu.h"
#include "csr-map.h"

int riscv_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        return gdb_get_regl(mem_buf, env->gpr[n]);
    } else if (n == 32) {
        return gdb_get_regl(mem_buf, env->pc);
    }
    return 0;
}

int riscv_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n == 0) {
        /* discard writes to x0 */
        return sizeof(target_ulong);
    } else if (n < 32) {
        env->gpr[n] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    } else if (n == 32) {
        env->pc = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    }
    return 0;
}

static int riscv_gdb_get_fpu(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        return gdb_get_reg64(mem_buf, env->fpr[n]);
    } else if (n < 35) {
        /*
         * CSR_FFLAGS is 0x001, and gdb says it is FP register 32, so we
         * subtract 31 to map the gdb FP register number to the CSR number.
         * This also works for CSR_FRM and CSR_FCSR.
         */
        return gdb_get_regl(mem_buf, csr_read_helper(env, n - 31, true));
    }
    return 0;
}

static int riscv_gdb_set_fpu(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        env->fpr[n] = ldq_p(mem_buf); /* always 64-bit */
        return sizeof(uint64_t);
    } else if (n < 35) {
        /*
         * CSR_FFLAGS is 0x001, and gdb says it is FP register 32, so we
         * subtract 31 to map the gdb FP register number to the CSR number.
         * This also works for CSR_FRM and CSR_FCSR.
         */
        csr_write_helper(env, ldtul_p(mem_buf), n - 31, true);
    }
    return 0;
}

static int riscv_gdb_get_csr(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < ARRAY_SIZE(csr_register_map)) {
        return gdb_get_regl(mem_buf, csr_read_helper(env, csr_register_map[n],
                                                     true));
    }
    return 0;
}

static int riscv_gdb_set_csr(CPURISCVState *env, uint8_t *mem_buf, int n)
{
    if (n < ARRAY_SIZE(csr_register_map)) {
        csr_write_helper(env, ldtul_p(mem_buf), csr_register_map[n], true);
    }
    return 0;
}

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    /* ??? Assume all targets have FPU regs for now.  */
#if defined(TARGET_RISCV32)
    gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                             35, "riscv-32bit-fpu.xml", 0);

    gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                             4096, "riscv-32bit-csr.xml", 0);
#elif defined(TARGET_RISCV64)
    gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                             35, "riscv-64bit-fpu.xml", 0);

    gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                             4096, "riscv-64bit-csr.xml", 0);
#endif
}
