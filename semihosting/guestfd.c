/*
 * Hosted file support for semihosting syscalls.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "semihosting/guestfd.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#else
#include "semihosting/softmmu-uaccess.h"
#endif

static GArray *guestfd_array;

/*
 * Allocate a new guest file descriptor and return it; if we
 * couldn't allocate a new fd then return -1.
 * This is a fairly simplistic implementation because we don't
 * expect that most semihosting guest programs will make very
 * heavy use of opening and closing fds.
 */
int alloc_guestfd(void)
{
    guint i;

    if (!guestfd_array) {
        /* New entries zero-initialized, i.e. type GuestFDUnused */
        guestfd_array = g_array_new(FALSE, TRUE, sizeof(GuestFD));
    }

    /* SYS_OPEN should return nonzero handle on success. Start guestfd from 1 */
    for (i = 1; i < guestfd_array->len; i++) {
        GuestFD *gf = &g_array_index(guestfd_array, GuestFD, i);

        if (gf->type == GuestFDUnused) {
            return i;
        }
    }

    /* All elements already in use: expand the array */
    g_array_set_size(guestfd_array, i + 1);
    return i;
}

/*
 * Look up the guestfd in the data structure; return NULL
 * for out of bounds, but don't check whether the slot is unused.
 * This is used internally by the other guestfd functions.
 */
static GuestFD *do_get_guestfd(int guestfd)
{
    if (!guestfd_array) {
        return NULL;
    }

    if (guestfd <= 0 || guestfd >= guestfd_array->len) {
        return NULL;
    }

    return &g_array_index(guestfd_array, GuestFD, guestfd);
}

/*
 * Given a guest file descriptor, get the associated struct.
 * If the fd is not valid, return NULL. This is the function
 * used by the various semihosting calls to validate a handle
 * from the guest.
 * Note: calling alloc_guestfd() or dealloc_guestfd() will
 * invalidate any GuestFD* obtained by calling this function.
 */
GuestFD *get_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    if (!gf || gf->type == GuestFDUnused) {
        return NULL;
    }
    return gf;
}

/*
 * Associate the specified guest fd (which must have been
 * allocated via alloc_fd() and not previously used) with
 * the specified host/gdb fd.
 */
void associate_guestfd(int guestfd, int hostfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = use_gdb_syscalls() ? GuestFDGDB : GuestFDHost;
    gf->hostfd = hostfd;
}

void staticfile_guestfd(int guestfd, const uint8_t *data, size_t len)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = GuestFDStatic;
    gf->staticfile.data = data;
    gf->staticfile.len = len;
    gf->staticfile.off = 0;
}

/*
 * Deallocate the specified guest file descriptor. This doesn't
 * close the host fd, it merely undoes the work of alloc_fd().
 */
void dealloc_guestfd(int guestfd)
{
    GuestFD *gf = do_get_guestfd(guestfd);

    assert(gf);
    gf->type = GuestFDUnused;
}

/*
 * Validate or compute the length of the string (including terminator).
 */
static int validate_strlen(CPUState *cs, target_ulong str, target_ulong tlen)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char c;

    if (tlen == 0) {
        ssize_t slen = target_strlen(str);

        if (slen < 0) {
            return -EFAULT;
        }
        if (slen >= INT32_MAX) {
            return -ENAMETOOLONG;
        }
        return slen + 1;
    }
    if (tlen > INT32_MAX) {
        return -ENAMETOOLONG;
    }
    if (get_user_u8(c, str + tlen - 1)) {
        return -EFAULT;
    }
    if (c != 0) {
        return -EINVAL;
    }
    return tlen;
}

static int validate_lock_user_string(char **pstr, CPUState *cs,
                                     target_ulong tstr, target_ulong tlen)
{
    int ret = validate_strlen(cs, tstr, tlen);
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *str = NULL;

    if (ret > 0) {
        str = lock_user(VERIFY_READ, tstr, ret, true);
        ret = str ? 0 : -EFAULT;
    }
    *pstr = str;
    return ret;
}

/*
 * File-related semihosting syscall implementations.
 */

static gdb_syscall_complete_cb gdb_open_complete;

static void gdb_open_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    if (!err) {
        int guestfd = alloc_guestfd();
        associate_guestfd(guestfd, ret);
        ret = guestfd;
    }
    gdb_open_complete(cs, ret, err);
}

static void gdb_open(CPUState *cs, gdb_syscall_complete_cb complete,
                     target_ulong fname, target_ulong fname_len,
                     int gdb_flags, int mode)
{
    int len = validate_strlen(cs, fname, fname_len);
    if (len < 0) {
        complete(cs, -1, -len);
        return;
    }

    gdb_open_complete = complete;
    gdb_do_syscall(gdb_open_cb, "open,%s,%x,%x",
                   fname, len, (target_ulong)gdb_flags, (target_ulong)mode);
}

static void host_open(CPUState *cs, gdb_syscall_complete_cb complete,
                      target_ulong fname, target_ulong fname_len,
                      int gdb_flags, int mode)
{
    CPUArchState *env G_GNUC_UNUSED = cs->env_ptr;
    char *p;
    int ret, host_flags;

    ret = validate_lock_user_string(&p, cs, fname, fname_len);
    if (ret < 0) {
        complete(cs, -1, -ret);
        return;
    }

    if (gdb_flags & GDB_O_WRONLY) {
        host_flags = O_WRONLY;
    } else if (gdb_flags & GDB_O_RDWR) {
        host_flags = O_RDWR;
    } else {
        host_flags = O_RDONLY;
    }
    if (gdb_flags & GDB_O_CREAT) {
        host_flags |= O_CREAT;
    }
    if (gdb_flags & GDB_O_TRUNC) {
        host_flags |= O_TRUNC;
    }
    if (gdb_flags & GDB_O_EXCL) {
        host_flags |= O_EXCL;
    }

    ret = open(p, host_flags, mode);
    if (ret < 0) {
        complete(cs, -1, errno);
    } else {
        int guestfd = alloc_guestfd();
        associate_guestfd(guestfd, ret);
        complete(cs, guestfd, 0);
    }
    unlock_user(p, fname, 0);
}

void semihost_sys_open(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       int gdb_flags, int mode)
{
    if (use_gdb_syscalls()) {
        gdb_open(cs, complete, fname, fname_len, gdb_flags, mode);
    } else {
        host_open(cs, complete, fname, fname_len, gdb_flags, mode);
    }
}
