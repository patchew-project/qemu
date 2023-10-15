/*
 *  Copyright (c) 2023 Bastian Koppelmann
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "qemu/log.h"

enum {
    SYS__OPEN        = 0x01,
    SYS__CLOSE       = 0x02,
    SYS__LSEEK       = 0x03,
    SYS__READ        = 0x04,
    SYS__WRITE       = 0x05,
    SYS__CREAT       = 0x06,
    SYS__UNLINK      = 0x07,
    SYS__STAT        = 0x08,
    SYS__FSTAT       = 0x09,
    SYS__GETTIME     = 0x0a,
};

enum {
    TARGET_EPERM        = 1,
    TARGET_ENOENT       = 2,
    TARGET_ESRCH        = 3,
    TARGET_EINTR        = 4,
    TARGET_EIO          = 5,
    TARGET_ENXIO        = 6,
    TARGET_E2BIG        = 7,
    TARGET_ENOEXEC      = 8,
    TARGET_EBADF        = 9,
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
    TARGET_ENOSYS       = 88,
    TARGET_ENOTEMPTY    = 90,
    TARGET_ENAMETOOLONG = 9
};

static int
tricore_vio_errno_h2g(int host_errno)
{
    switch (host_errno) {
    case EPERM:
        return TARGET_EPERM;
    case ENOENT:
        return TARGET_ENOENT;
    case ESRCH:
        return TARGET_ESRCH;
    case EINTR:
        return TARGET_EINTR;
    case EIO:
        return TARGET_EIO;
    case ENXIO:
        return TARGET_ENXIO;
    case E2BIG:
        return TARGET_E2BIG;
    case ENOEXEC:
        return TARGET_ENOEXEC;
    case EBADF:
        return TARGET_EBADF;
    case ECHILD:
        return TARGET_ECHILD;
    case EAGAIN:
        return TARGET_EAGAIN;
    case ENOMEM:
        return TARGET_ENOMEM;
    case EACCES:
        return TARGET_EACCES;
    case EFAULT:
        return TARGET_EFAULT;
    case ENOTBLK:
        return TARGET_ENOTBLK;
    case EBUSY:
        return TARGET_EBUSY;
    case EEXIST:
        return TARGET_EEXIST;
    case EXDEV:
        return TARGET_EXDEV;
    case ENODEV:
        return TARGET_ENODEV;
    case ENOTDIR:
        return TARGET_ENOTDIR;
    case EISDIR:
        return TARGET_EISDIR;
    case EINVAL:
        return TARGET_EINVAL;
    case ENFILE:
        return TARGET_ENFILE;
    case EMFILE:
        return TARGET_EMFILE;
    case ENOTTY:
        return TARGET_ENOTTY;
    case ETXTBSY:
        return TARGET_ETXTBSY;
    case EFBIG:
        return TARGET_EFBIG;
    case ENOSPC:
        return TARGET_ENOSPC;
    case ESPIPE:
        return TARGET_ESPIPE;
    case EROFS:
        return TARGET_EROFS;
    case EMLINK:
        return TARGET_EMLINK;
    case EPIPE:
        return TARGET_EPIPE;
    case ENOSYS:
        return TARGET_ENOSYS;
    case ENOTEMPTY:
        return TARGET_ENOTEMPTY;
    case ENAMETOOLONG:
        return TARGET_ENAMETOOLONG;
    default:
        return host_errno;
    }
}

/*
 * Set return and errno values;  the ___virtio function takes care
 * that the target's errno variable gets updated from %d12, and
 * eventually moves %d11 to the return register (%d2).
 */
static void tricore_vio_set_result(CPUTriCoreState *env, int retval,
                                   int host_errno)
{
    env->gpr_d[11] = retval;
    env->gpr_d[12] = tricore_vio_errno_h2g(host_errno);
}

static void tricore_vio_readwrite(CPUTriCoreState *env, bool is_write)
{
    CPUState *cs = env_cpu(env);
    hwaddr paddr, sz;
    uint32_t page_left, io_sz, vaddr;
    size_t count;
    ssize_t ret = 0;

    int fd = env->gpr_d[4];
    vaddr  = env->gpr_a[4];
    count = env->gpr_d[5];

    while (count > 0) {
        paddr = cpu_get_phys_page_debug(cs, vaddr);
        page_left = TARGET_PAGE_SIZE - (vaddr & (TARGET_PAGE_SIZE - 1));
        io_sz = page_left < count ? page_left : count;
        sz = io_sz;
        void *buf = cpu_physical_memory_map(paddr, &sz, true);

        if (buf) {
            vaddr += io_sz;
            count -= io_sz;
            ret = is_write ?
                write(fd, buf, io_sz) :
                read(fd, buf, io_sz);
            if (ret == -1) {
                ret = 0;
                tricore_vio_set_result(env, ret, EINVAL);
            } else {
                tricore_vio_set_result(env, ret, errno);
            }
        }
        cpu_physical_memory_unmap(buf, sz, !is_write, ret);
    }
}

static void tricore_vio_read(CPUTriCoreState *env)
{
    tricore_vio_readwrite(env, false);
}

static void tricore_vio_write(CPUTriCoreState *env)
{
    tricore_vio_readwrite(env, true);
}


#define TRICORE_VIO_MARKER 0x6f69765f /* "_vio" */
#define TRICORE_VIO_EXIT_MARKER 0xE60
#define TRICORE_VIO_SIMTEST_SUCC 0x900d
void helper_tricore_semihost(CPUTriCoreState *env, uint32_t pc)
{
    int syscall;
    uint32_t marker = cpu_ldl_code(env, pc - 4);

    /* check for exit marker */
    if (extract32(marker, 0, 12) == TRICORE_VIO_EXIT_MARKER) {
        if (env->gpr_a[14] == TRICORE_VIO_SIMTEST_SUCC) {
            exit(0);
        } else {
            exit(env->gpr_a[14]);
        }
    }

    if (marker != TRICORE_VIO_MARKER) {
        return;
    }

    syscall = (int)env->gpr_d[12];
    switch (syscall) {
    case SYS__READ:
        tricore_vio_read(env);
        break;
    case SYS__WRITE:
        tricore_vio_write(env);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s(%d): not implemented\n", __func__,
                      syscall);
        tricore_vio_set_result(env, -1, ENOSYS);
        break;
    }
}
