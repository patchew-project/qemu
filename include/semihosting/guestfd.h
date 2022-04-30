/*
 * Hosted file support for semihosting syscalls.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOSTING_GUESTFD_H
#define SEMIHOSTING_GUESTFD_H

typedef enum GuestFDType {
    GuestFDUnused = 0,
    GuestFDHost = 1,
    GuestFDGDB = 2,
    GuestFDStatic = 3,
} GuestFDType;

/*
 * Guest file descriptors are integer indexes into an array of
 * these structures (we will dynamically resize as necessary).
 */
typedef struct GuestFD {
    GuestFDType type;
    union {
        int hostfd;
        struct {
            const uint8_t *data;
            size_t len;
            size_t off;
        } staticfile;
    };
} GuestFD;

int alloc_guestfd(void);
void dealloc_guestfd(int guestfd);
GuestFD *get_guestfd(int guestfd);

void associate_guestfd(int guestfd, int hostfd);
void staticfile_guestfd(int guestfd, const uint8_t *data, size_t len);

/*
 * Syscall implementations for semi-hosting.  Argument loading from
 * the guest is performed by the caller; results are returned via
 * the 'complete' callback.  String operands are in address/len pairs.
 * The len argument may be 0 (when the semihosting abi does not
 * already provide the length), or non-zero (where it should include
 * the terminating zero).
 */

void semihost_sys_open(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       int gdb_flags, int mode);

void semihost_sys_close(CPUState *cs, gdb_syscall_complete_cb complete, int fd);

void semihost_sys_read(CPUState *cs, gdb_syscall_complete_cb complete,
                       int fd, target_ulong buf, target_ulong len);

void semihost_sys_write(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, target_ulong buf, target_ulong len);

void semihost_sys_lseek(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, int64_t off, int gdb_whence);

void semihost_sys_isatty(CPUState *cs, gdb_syscall_complete_cb complete,
                         int fd);

#endif /* SEMIHOSTING_GUESTFD_H */
