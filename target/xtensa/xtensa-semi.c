/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "semihosting/semihost.h"
#include "semihosting/syscalls.h"
#include "semihosting/softmmu-uaccess.h"
#include "qapi/error.h"
#include "qemu/log.h"

enum {
    TARGET_SYS_exit = 1,
    TARGET_SYS_read = 3,
    TARGET_SYS_write = 4,
    TARGET_SYS_open = 5,
    TARGET_SYS_close = 6,
    TARGET_SYS_lseek = 19,
    TARGET_SYS_select_one = 29,

    TARGET_SYS_argc = 1000,
    TARGET_SYS_argv_sz = 1001,
    TARGET_SYS_argv = 1002,
    TARGET_SYS_memset = 1004,
};

enum {
    SELECT_ONE_READ   = 1,
    SELECT_ONE_WRITE  = 2,
    SELECT_ONE_EXCEPT = 3,
};

enum {
    TARGET_EPERM        =  1,
    TARGET_ENOENT       =  2,
    TARGET_ESRCH        =  3,
    TARGET_EINTR        =  4,
    TARGET_EIO          =  5,
    TARGET_ENXIO        =  6,
    TARGET_E2BIG        =  7,
    TARGET_ENOEXEC      =  8,
    TARGET_EBADF        =  9,
    TARGET_ECHILD       = 10,
    TARGET_EAGAIN       = 11,
    TARGET_ENOMEM       = 12,
    TARGET_EACCES       = 13,
    TARGET_EFAULT       = 14,
    TARGET_ENOTBLK      = 15,
    TARGET_EBUSY        = 16,
    TARGET_EEXIST       = 17,
    TARGET_EXDEV        = 18,
    TARGET_ENODEV       = 19,
    TARGET_ENOTDIR      = 20,
    TARGET_EISDIR       = 21,
    TARGET_EINVAL       = 22,
    TARGET_ENFILE       = 23,
    TARGET_EMFILE       = 24,
    TARGET_ENOTTY       = 25,
    TARGET_ETXTBSY      = 26,
    TARGET_EFBIG        = 27,
    TARGET_ENOSPC       = 28,
    TARGET_ESPIPE       = 29,
    TARGET_EROFS        = 30,
    TARGET_EMLINK       = 31,
    TARGET_EPIPE        = 32,
    TARGET_EDOM         = 33,
    TARGET_ERANGE       = 34,
    TARGET_ENOSYS       = 88,
    TARGET_ELOOP        = 92,
};

static uint32_t errno_h2g(int host_errno)
{
    switch (host_errno) {
    case 0:         return 0;
    case EPERM:     return TARGET_EPERM;
    case ENOENT:    return TARGET_ENOENT;
    case ESRCH:     return TARGET_ESRCH;
    case EINTR:     return TARGET_EINTR;
    case EIO:       return TARGET_EIO;
    case ENXIO:     return TARGET_ENXIO;
    case E2BIG:     return TARGET_E2BIG;
    case ENOEXEC:   return TARGET_ENOEXEC;
    case EBADF:     return TARGET_EBADF;
    case ECHILD:    return TARGET_ECHILD;
    case EAGAIN:    return TARGET_EAGAIN;
    case ENOMEM:    return TARGET_ENOMEM;
    case EACCES:    return TARGET_EACCES;
    case EFAULT:    return TARGET_EFAULT;
#ifdef ENOTBLK
    case ENOTBLK:   return TARGET_ENOTBLK;
#endif
    case EBUSY:     return TARGET_EBUSY;
    case EEXIST:    return TARGET_EEXIST;
    case EXDEV:     return TARGET_EXDEV;
    case ENODEV:    return TARGET_ENODEV;
    case ENOTDIR:   return TARGET_ENOTDIR;
    case EISDIR:    return TARGET_EISDIR;
    case EINVAL:    return TARGET_EINVAL;
    case ENFILE:    return TARGET_ENFILE;
    case EMFILE:    return TARGET_EMFILE;
    case ENOTTY:    return TARGET_ENOTTY;
#ifdef ETXTBSY
    case ETXTBSY:   return TARGET_ETXTBSY;
#endif
    case EFBIG:     return TARGET_EFBIG;
    case ENOSPC:    return TARGET_ENOSPC;
    case ESPIPE:    return TARGET_ESPIPE;
    case EROFS:     return TARGET_EROFS;
    case EMLINK:    return TARGET_EMLINK;
    case EPIPE:     return TARGET_EPIPE;
    case EDOM:      return TARGET_EDOM;
    case ERANGE:    return TARGET_ERANGE;
    case ENOSYS:    return TARGET_ENOSYS;
#ifdef ELOOP
    case ELOOP:     return TARGET_ELOOP;
#endif
    };

    return TARGET_EINVAL;
}

static void xtensa_cb(CPUState *cs, uint64_t ret, int err)
{
    CPUXtensaState *env = cs->env_ptr;

    env->regs[3] = errno_h2g(err);
    env->regs[2] = ret;
}

static void xtensa_select_cb(CPUState *cs, uint64_t ret, int err)
{
    if (ret & G_IO_NVAL) {
        xtensa_cb(cs, -1, EBADF);
    } else {
        xtensa_cb(cs, ret != 0, 0);
    }
}

void xtensa_semihosting(CPUXtensaState *env)
{
    CPUState *cs = env_cpu(env);
    uint32_t *regs = env->regs;

    switch (regs[2]) {
    case TARGET_SYS_exit:
        gdb_exit(regs[3]);
        exit(regs[3]);
        break;

    case TARGET_SYS_read:
        semihost_sys_read(cs, xtensa_cb, regs[3], regs[4], regs[5]);
        break;
    case TARGET_SYS_write:
        semihost_sys_write(cs, xtensa_cb, regs[3], regs[4], regs[5]);
        break;
    case TARGET_SYS_open:
        semihost_sys_open(cs, xtensa_cb, regs[3], 0, regs[4], regs[5]);
        break;
    case TARGET_SYS_close:
        semihost_sys_close(cs, xtensa_cb, regs[3]);
        break;
    case TARGET_SYS_lseek:
        semihost_sys_lseek(cs, xtensa_cb, regs[3], regs[4], regs[5]);
        break;

    case TARGET_SYS_select_one:
        {
            int timeout, events;

            if (regs[5]) {
                uint32_t tv_sec, tv_usec;
                uint64_t msec;

                if (get_user_u32(tv_sec, regs[5]) ||
                    get_user_u32(tv_usec, regs[5])) {
                    xtensa_cb(cs, -1, EFAULT);
                    return;
                }

                /* Poll timeout is in milliseconds; overflow to infinity. */
                msec = tv_sec * 1000ull + DIV_ROUND_UP(tv_usec, 1000ull);
                timeout = msec <= INT32_MAX ? msec : -1;
            } else {
                timeout = -1;
            }

            switch (regs[4]) {
            case SELECT_ONE_READ:
                events = G_IO_IN;
                break;
            case SELECT_ONE_WRITE:
                events = G_IO_OUT;
                break;
            case SELECT_ONE_EXCEPT:
                events = G_IO_PRI;
                break;
            default:
                xtensa_cb(cs, -1, EINVAL);
                return;
            }

            semihost_sys_poll_one(cs, xtensa_select_cb,
                                  regs[3], events, timeout);
        }
        break;

    case TARGET_SYS_argc:
        regs[2] = semihosting_get_argc();
        regs[3] = 0;
        break;

    case TARGET_SYS_argv_sz:
        {
            int argc = semihosting_get_argc();
            int sz = (argc + 1) * sizeof(uint32_t);
            int i;

            for (i = 0; i < argc; ++i) {
                sz += 1 + strlen(semihosting_get_arg(i));
            }
            regs[2] = sz;
            regs[3] = 0;
        }
        break;

    case TARGET_SYS_argv:
        {
            int argc = semihosting_get_argc();
            int str_offset = (argc + 1) * sizeof(uint32_t);
            int i;
            uint32_t argptr;

            for (i = 0; i < argc; ++i) {
                const char *str = semihosting_get_arg(i);
                int str_size = strlen(str) + 1;

                argptr = tswap32(regs[3] + str_offset);

                cpu_memory_rw_debug(cs,
                                    regs[3] + i * sizeof(uint32_t),
                                    (uint8_t *)&argptr, sizeof(argptr), 1);
                cpu_memory_rw_debug(cs,
                                    regs[3] + str_offset,
                                    (uint8_t *)str, str_size, 1);
                str_offset += str_size;
            }
            argptr = 0;
            cpu_memory_rw_debug(cs,
                                regs[3] + i * sizeof(uint32_t),
                                (uint8_t *)&argptr, sizeof(argptr), 1);
            regs[3] = 0;
        }
        break;

    case TARGET_SYS_memset:
        {
            uint32_t base = regs[3];
            uint32_t sz = regs[5];

            while (sz) {
                hwaddr len = sz;
                void *buf = cpu_physical_memory_map(base, &len, 1);

                if (buf && len) {
                    memset(buf, regs[4], len);
                    cpu_physical_memory_unmap(buf, len, 1, len);
                } else {
                    len = 1;
                }
                base += len;
                sz -= len;
            }
            regs[2] = regs[3];
            regs[3] = 0;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s(%d): not implemented\n", __func__, regs[2]);
        regs[2] = -1;
        regs[3] = TARGET_ENOSYS;
        break;
    }
}
