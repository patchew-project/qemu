/*
 *  gdbsim semihosting syscall interface.
 *  The semihosting protocol implemented here is described in
 *
 *  libgloss sources:
 *  https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git;a=blob;f=libgloss/syscall.h;hb=HEAD
 *
 *  gdb sources:
 *  https://sourceware.org/git/gitweb.cgi?p=binutils-gdb.git;a=blob;f=sim/rx/syscalls.c;hb=HEAD
 *
 *  Copyright (c) 2022 Linaro, Ltd.
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
#include "cpu.h"
#include "exec/gdbstub.h"
#include "semihosting/syscalls.h"
#include "qemu/log.h"

/*
 * These are the syscall numbers from libgloss/syscall.h,
 * but note that not all of them are implemented.
 */
enum {
    TARGET_SYS_exit = 1,
    TARGET_SYS_open,
    TARGET_SYS_close,
    TARGET_SYS_read,
    TARGET_SYS_write,
    TARGET_SYS_lseek,
    TARGET_SYS_unlink,
    TARGET_SYS_getpid,
    TARGET_SYS_kill,
    TARGET_SYS_fstat,
    TARGET_SYS_sbrk,
    TARGET_SYS_argvlen,
    TARGET_SYS_argv,
    TARGET_SYS_chdir,
    TARGET_SYS_stat,
    TARGET_SYS_chmod,
    TARGET_SYS_utime,
    TARGET_SYS_time,
    TARGET_SYS_gettimeofday,
    TARGET_SYS_times,
    TARGET_SYS_link,
    TARGET_SYS_argc,
    TARGET_SYS_argnlen,
    TARGET_SYS_argn,
    TARGET_SYS_reconfig,
};

static void rx_semi_cb(CPUState *cs, uint64_t ret, int err)
{
    CPURXState *env = cs->env_ptr;

    /* There is no concept of errno in this interface. */
    env->regs[1] = ret;
}

static bool rx_semi_arg(CPURXState *env, uint32_t *ret, int argn)
{
    if (argn < 4) {
        *ret = env->regs[argn + 1];
    } else {
        uint32_t stack_addr = env->regs[0] + 4 + (argn - 4) * 4;
        if (cpu_memory_rw_debug(env_cpu(env), stack_addr, ret, 4, 0)) {
            return false;
        }
        tswap32s(ret);
    }
    return true;
}

#define GET_ARG(E, N) \
    ({ uint32_t v_; if (!rx_semi_arg((E), &v_, (N))) goto failed; v_; })

void rx_cpu_do_semihosting(CPURXState *env)
{
    CPUState *cs = env_cpu(env);
    uint32_t nr = env->regs[5];
    uint32_t a0, a1, a2;

    switch (nr) {
    case TARGET_SYS_exit:
        a0 = GET_ARG(env, 0);
        gdb_exit(a0);
        exit(a0);

    case TARGET_SYS_open:
        /*
         * This function is declared int open(char *path, int flags, ...),
         * which means that only the first argument is in registers.
         */
        a0 = GET_ARG(env, 0);
        a1 = GET_ARG(env, 4);
        a2 = GET_ARG(env, 5);
        semihost_sys_open(cs, rx_semi_cb, a0, 0, a1, a2);
        break;

    case TARGET_SYS_close:
        a0 = GET_ARG(env, 0);
        semihost_sys_close(cs, rx_semi_cb, a0);
        break;

    case TARGET_SYS_read:
        a0 = GET_ARG(env, 0);
        a1 = GET_ARG(env, 1);
        a2 = GET_ARG(env, 2);
        semihost_sys_read(cs, rx_semi_cb, a0, a1, a2);
        break;

    case TARGET_SYS_write:
        a0 = GET_ARG(env, 0);
        a1 = GET_ARG(env, 1);
        a2 = GET_ARG(env, 2);
        semihost_sys_write(cs, rx_semi_cb, a0, a1, a2);
        break;

    case TARGET_SYS_getpid:
        rx_semi_cb(cs, 42, 0);
        break;

    case TARGET_SYS_gettimeofday:
        a0 = GET_ARG(env, 0);
        semihost_sys_gettimeofday(cs, rx_semi_cb, a0, 0);
        break;

    case TARGET_SYS_kill:
        a0 = GET_ARG(env, 0);
        if (a0 != 42) {
            goto failed;
        }
        /* Without defined signal numbers, pretend they're all SIGABRT. */
        gdb_exit(-1);
        abort();

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "rx-semihosting: unsupported "
                      "semihosting syscall %u\n", nr);
        /* fall through */

    failed:
        rx_semi_cb(cs, -1, 0);
        break;
    }

    /*
     * Skip the semihosting insn (int #255).
     * Must be done after any cpu_loop_exit() within the syscalls.
     */
    env->pc += 3;
}
