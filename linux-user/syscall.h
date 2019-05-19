/*
 *  Linux syscalls internals
 *  Copyright (c) 2018 Linaro, Limited.
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

#ifndef LINUX_USER_SYSCALL_H
#define LINUX_USER_SYSCALL_H 1

typedef struct SyscallDef SyscallDef;

/*
 * This hook extracts max 6 arguments from max 8 input registers.
 * In the process, register pairs that store 64-bit arguments are merged.
 * Finally, syscalls are demultipliexed; e.g. the hook for socketcall will
 * return the SyscallDef for bind, listen, etc.  In the process the hook
 * may need to read from guest memory, or otherwise validate operands.
 * On failure, set errno (to a host value) and return NULL;
 * the (target adjusted) errno will be returned to the guest.
 */
typedef const SyscallDef *SyscallArgsFn(const SyscallDef *, int64_t out[6],
                                        abi_long in[8]);

/* This hook implements the syscall.  */
typedef abi_long SyscallImplFn(CPUArchState *, int64_t, int64_t, int64_t,
                               int64_t, int64_t, int64_t);

/* This hook prints the arguments to the syscall for strace.  */
typedef void SyscallPrintFn(const SyscallDef *, int64_t arg[6]);

/* This hook print the return value from the syscall for strace.  */
typedef void SyscallPrintRetFn(const SyscallDef *, abi_long);

/*
 * These flags describe the arguments for the generic fallback to
 * SyscallPrintFn.  ARG_NONE indicates that the argument is not present.
 */
typedef enum {
    ARG_NONE = 0,

    /* These print as numbers of abi_long.  */
    ARG_DEC,
    ARG_HEX,
    ARG_OCT,

    /* These numbers are interpreted.  */
    ARG_ATDIRFD,
    ARG_SIGNAL,
    ARG_LSEEKWHENCE,

    /* These print as sets of flags.  */
    ARG_ACCESSFLAG,
    ARG_ATFLAG,
    ARG_CLONEFLAG,
    ARG_MMAPFLAG,
    ARG_MMAPPROT,
    ARG_MODEFLAG,
    ARG_MOUNTFLAG,
    ARG_OPENFLAG,
    ARG_UMOUNTFLAG,
    ARG_UNLINKATFLAG,

    /* These are interpreted as pointers.  */
    ARG_PTR,
    ARG_STR,
    ARG_BUF,

    /* For a 32-bit host, force printing as a 64-bit operand.  */
#if TARGET_ABI_BITS == 32
    ARG_DEC64,
#else
    ARG_DEC64 = ARG_DEC,
#endif
} SyscallArgType;

struct SyscallDef {
    const char *name;
    SyscallArgsFn *args;
    SyscallImplFn *impl;
    SyscallPrintFn *print;
    SyscallPrintRetFn *print_ret;
    SyscallArgType arg_type[6];
};

void print_syscall_def(const SyscallDef *def, int64_t args[6]);
void print_syscall_def_ret(const SyscallDef *def, abi_long ret);
void print_syscall_ptr_ret(const SyscallDef *def, abi_long ret);

#endif
