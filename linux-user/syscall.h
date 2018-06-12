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

typedef struct SyscallDef SyscallDef;

/* This hook extracts max 6 arguments from max 8 input registers.
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

/* These flags describe the arguments for the generic fallback to
 * SyscallPrintFn.  ARG_NONE indicates that the argument is not present.
 */
typedef enum {
    ARG_NONE = 0,

    /* These print as numbers of abi_long.  */
    ARG_DEC,
    ARG_HEX,
    ARG_OCT,

    /* These print as sets of flags.  */
    ARG_ATDIRFD,
    ARG_ATFLAG,
    ARG_MODEFLAG,
    ARG_OPENFLAG,

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

/* Emit the signature for a SyscallArgsFn.  */
#define SYSCALL_ARGS(NAME) \
    static const SyscallDef *args_##NAME(const SyscallDef *def, \
                                         int64_t out[6], abi_long in[8])

/* Emit the signature for a SyscallImplFn.  */
#define SYSCALL_IMPL(NAME) \
    static abi_long impl_##NAME(CPUArchState *cpu_env, int64_t arg1, \
                                int64_t arg2, int64_t arg3, int64_t arg4, \
                                int64_t arg5, int64_t arg6)

/* Emit the definition for a "simple" syscall.  Such does not use
 * SyscallArgsFn and only uses arg_type for strace.
 */
#define SYSCALL_DEF(NAME, ...) \
    const SyscallDef def_##NAME = { \
        .name = #NAME, .impl = impl_##NAME, .arg_type = { __VA_ARGS__ } \
    }

/* Emit the definition for a syscall that also has an args hook,
 * and uses arg_type for strace.
 */
#define SYSCALL_DEF_ARGS(NAME, ...) \
    const SyscallDef def_##NAME = { \
        .name = #NAME, .args = args_##NAME, .impl = impl_##NAME, \
        .arg_type = { __VA_ARGS__ } \
    }

/* Declarations from the main syscall.c for use in syscall_foo.c,
 * or for the moment, vice versa.
 */

int host_to_target_errno(int err);

static inline abi_long get_errno(abi_long ret)
{
    return unlikely(ret == -1) ? -host_to_target_errno(errno) : ret;
}

static inline int is_error(abi_ulong ret)
{
    return ret >= -4096;
}

typedef abi_long (*TargetFdDataFunc)(void *, size_t);
typedef abi_long (*TargetFdAddrFunc)(void *, abi_ulong, socklen_t);
typedef struct TargetFdTrans {
    TargetFdDataFunc host_to_target_data;
    TargetFdDataFunc target_to_host_data;
    TargetFdAddrFunc target_to_host_addr;
} TargetFdTrans;

extern TargetFdTrans **target_fd_trans;
extern unsigned int target_fd_max;

static inline TargetFdDataFunc fd_trans_target_to_host_data(int fd)
{
    if (fd >= 0 && fd < target_fd_max && target_fd_trans[fd]) {
        return target_fd_trans[fd]->target_to_host_data;
    }
    return NULL;
}

static inline TargetFdDataFunc fd_trans_host_to_target_data(int fd)
{
    if (fd >= 0 && fd < target_fd_max && target_fd_trans[fd]) {
        return target_fd_trans[fd]->host_to_target_data;
    }
    return NULL;
}

static inline TargetFdAddrFunc fd_trans_target_to_host_addr(int fd)
{
    if (fd >= 0 && fd < target_fd_max && target_fd_trans[fd]) {
        return target_fd_trans[fd]->target_to_host_addr;
    }
    return NULL;
}

void fd_trans_register(int fd, TargetFdTrans *trans);

static inline void fd_trans_unregister(int fd)
{
    if (fd >= 0 && fd < target_fd_max) {
        target_fd_trans[fd] = NULL;
    }
}

struct iovec *lock_iovec(int type, abi_ulong target_addr,
                         abi_ulong count, int copy);
void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
                  abi_ulong count, int copy);

/* Returns true if syscall NUM expects 64bit types aligned even
 * on pairs of registers.
 */
static inline bool regpairs_aligned(void *cpu_env, int num)
{
#ifdef TARGET_ARM
    return ((CPUARMState *)cpu_env)->eabi;
#elif defined(TARGET_MIPS) && TARGET_ABI_BITS == 32
    return true;
#elif defined(TARGET_PPC) && !defined(TARGET_PPC64)
    /* SysV AVI for PPC32 expects 64bit parameters to be passed on
     * odd/even pairs of registers which translates to the same as
     * we start with r3 as arg1
     */
    return true;
#elif defined(TARGET_SH4)
    /* SH4 doesn't align register pairs, except for p{read,write}64 */
    switch (num) {
    case TARGET_NR_pread64:
    case TARGET_NR_pwrite64:
        return true;
    default:
        return false;
    }
#elif defined(TARGET_XTENSA)
    return true;
#else
    return false;
#endif
}

static inline uint64_t target_offset64(abi_ulong word0, abi_ulong word1)
{
#if TARGET_ABI_BITS == 32
# ifdef TARGET_WORDS_BIGENDIAN
    return ((uint64_t)word0 << 32) | word1;
# else
    return ((uint64_t)word1 << 32) | word0;
# endif
#else
    return word0;
#endif
}

/* Temporary declarations from syscall_foo.c back to main syscall.c.
 * These indicate incomplete conversion.
 */

int is_proc_myself(const char *filename, const char *entry);
extern bitmask_transtbl const fcntl_flags_tbl[];

/* Declarators for interruptable system calls.  */

#define safe_syscall0(type, name) \
static type safe_##name(void) \
{ \
    return safe_syscall(__NR_##name); \
}

#define safe_syscall1(type, name, type1, arg1) \
static type safe_##name(type1 arg1) \
{ \
    return safe_syscall(__NR_##name, arg1); \
}

#define safe_syscall2(type, name, type1, arg1, type2, arg2) \
static type safe_##name(type1 arg1, type2 arg2) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2); \
}

#define safe_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3); \
}

#define safe_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3, arg4); \
}

#define safe_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5); \
}

#define safe_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5, type6, arg6) \
static type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5, type6 arg6) \
{ \
    return safe_syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5, arg6); \
}

/* Include declarations of syscall definitions.  */
#include "syscall_list.h"
