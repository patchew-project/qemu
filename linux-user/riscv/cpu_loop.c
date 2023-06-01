/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu.h"
#include "user-internals.h"
#include "cpu_loop-common.h"
#include "signal-common.h"
#include "elf.h"
#include "semihosting/common-semi.h"

#define RISCV_HWPROBE_KEY_MVENDORID     0
#define RISCV_HWPROBE_KEY_MARCHID       1
#define RISCV_HWPROBE_KEY_MIMPID        2

#define RISCV_HWPROBE_KEY_BASE_BEHAVIOR 3
#define     RISCV_HWPROBE_BASE_BEHAVIOR_IMA (1 << 0)

#define RISCV_HWPROBE_KEY_IMA_EXT_0     4
#define     RISCV_HWPROBE_IMA_FD       (1 << 0)
#define     RISCV_HWPROBE_IMA_C        (1 << 1)

#define RISCV_HWPROBE_KEY_CPUPERF_0     5
#define     RISCV_HWPROBE_MISALIGNED_UNKNOWN     (0 << 0)
#define     RISCV_HWPROBE_MISALIGNED_EMULATED    (1 << 0)
#define     RISCV_HWPROBE_MISALIGNED_SLOW        (2 << 0)
#define     RISCV_HWPROBE_MISALIGNED_FAST        (3 << 0)
#define     RISCV_HWPROBE_MISALIGNED_UNSUPPORTED (4 << 0)
#define     RISCV_HWPROBE_MISALIGNED_MASK        (7 << 0)

struct riscv_hwprobe {
    int64_t  key;
    uint64_t value;
};

static void hwprobe_one_pair(CPURISCVState *env, struct riscv_hwprobe *pair)
{
    const RISCVCPUConfig *cfg = riscv_cpu_cfg(env);

    pair->value = 0;

    switch (pair->key) {
    case RISCV_HWPROBE_KEY_MVENDORID:
        pair->value = cfg->mvendorid;
        break;
    case RISCV_HWPROBE_KEY_MARCHID:
        pair->value = cfg->marchid;
        break;
    case RISCV_HWPROBE_KEY_MIMPID:
        pair->value = cfg->mimpid;
        break;
    case RISCV_HWPROBE_KEY_BASE_BEHAVIOR:
        pair->value = riscv_has_ext(env, RVI) &&
                      riscv_has_ext(env, RVM) &&
                      riscv_has_ext(env, RVA) ?
                      RISCV_HWPROBE_BASE_BEHAVIOR_IMA : 0;
        break;
    case RISCV_HWPROBE_KEY_IMA_EXT_0:
        pair->value = riscv_has_ext(env, RVF) &&
                      riscv_has_ext(env, RVD) ?
                      RISCV_HWPROBE_IMA_FD : 0;
        pair->value |= riscv_has_ext(env, RVC) ?
                       RISCV_HWPROBE_IMA_C : pair->value;
        break;
    case RISCV_HWPROBE_KEY_CPUPERF_0:
        pair->value = RISCV_HWPROBE_MISALIGNED_UNKNOWN;
        break;
    default:
        pair->key = -1;
    break;
    }
}

static long sys_riscv_hwprobe(CPURISCVState *env,
                              abi_ulong user_pairs,
                              size_t pair_count,
                              size_t cpu_count,
                              abi_ulong user_cpus,
                              unsigned int flags)
{
    struct riscv_hwprobe *host_pairs;
    cpu_set_t *host_cpus = NULL;
    size_t cpu_setsize = 0;

    /* flags must be 0 */
    if (flags != 0) {
        return 1
    };

    /* inconsistence cpu_set */
    if (cpu_count != 0 && user_cpus == 0) {
        return 1;
    }

    host_pairs = lock_user(VERIFY_WRITE, user_pairs,
                           sizeof(*host_pairs) * pair_count, 0);

    if (host_pairs == NULL) {
        return 1;
    }

    if (user_cpus != 0) {
        cpu_setsize = CPU_ALLOC_SIZE(user_cpus);
        host_cpus = lock_user(VERIFY_READ, user_cpus, cpu_setsize, 0);
    }

    /* cpuset is ignored, symmetric CPUs in qemu */

    for (struct riscv_hwprobe *ipairs = host_pairs;
         pair_count > 0;
         pair_count--, ipairs++) {
        hwprobe_one_pair(env, ipairs);
    }

    if (host_cpus != 0) {
        unlock_user(host_cpus, user_cpus, cpu_setsize);
    }

    unlock_user(host_pairs, user_pairs, sizeof(*host_pairs) * pair_count);
    return 0;
};

void cpu_loop(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    target_ulong ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case RISCV_EXCP_U_ECALL:
            env->pc += 4;
            if (env->gpr[xA7] == TARGET_NR_arch_specific_syscall + 14) {
                /* riscv_hwprobe */
                ret = sys_riscv_hwprobe(env,
                                        env->gpr[xA0], env->gpr[xA1],
                                        env->gpr[xA2], env->gpr[xA3],
                                        env->gpr[xA4]);
            } else if (env->gpr[xA7] == TARGET_NR_arch_specific_syscall + 15) {
                /* riscv_flush_icache_syscall is a no-op in QEMU as
                   self-modifying code is automatically detected */
                ret = 0;
            } else {
                ret = do_syscall(env,
                                 env->gpr[(env->elf_flags & EF_RISCV_RVE)
                                    ? xT0 : xA7],
                                 env->gpr[xA0],
                                 env->gpr[xA1],
                                 env->gpr[xA2],
                                 env->gpr[xA3],
                                 env->gpr[xA4],
                                 env->gpr[xA5],
                                 0, 0);
            }
            if (ret == -QEMU_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -QEMU_ESIGRETURN) {
                env->gpr[xA0] = ret;
            }
            if (cs->singlestep_enabled) {
                goto gdbstep;
            }
            break;
        case RISCV_EXCP_ILLEGAL_INST:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->pc);
            break;
        case RISCV_EXCP_BREAKPOINT:
        case EXCP_DEBUG:
        gdbstep:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;
        case RISCV_EXCP_SEMIHOST:
            do_common_semihosting(cs);
            env->pc += 4;
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            exit(EXIT_FAILURE);
        }

        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    struct image_info *info = ts->info;

    env->pc = regs->sepc;
    env->gpr[xSP] = regs->sp;
    env->elf_flags = info->elf_flags;

    if ((env->misa_ext & RVE) && !(env->elf_flags & EF_RISCV_RVE)) {
        error_report("Incompatible ELF: RVE cpu requires RVE ABI binary");
        exit(EXIT_FAILURE);
    }

    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
}
