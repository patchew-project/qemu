/*
 *  Linux fcntl syscall implementation
 *  Copyright (c) 2003 Fabrice Bellard
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

typedef struct FcntlEntry FcntlEntry;

typedef abi_long FcntlFn(int fd, int host_cmd, abi_long arg);

struct FcntlEntry {
    const char *name;
    FcntlFn *host_fn;
    int host_cmd;
    SyscallArgType arg_type;
};

static abi_long do_fcntl_int(int fd, int host_cmd, abi_long arg)
{
    return get_errno(safe_fcntl(fd, host_cmd, arg));
}

static abi_long do_fcntl_getfl(int fd, int host_cmd, abi_long arg)
{
    abi_long ret = get_errno(safe_fcntl(fd, host_cmd));
    if (!is_error(ret)) {
        ret = host_to_target_bitmask(ret, fcntl_flags_tbl);
    }
    return ret;
}

static abi_long do_fcntl_setfl(int fd, int host_cmd, abi_long arg)
{
    return get_errno(safe_fcntl(fd, host_cmd,
                                target_to_host_bitmask(arg, fcntl_flags_tbl)));
}

static abi_long do_fcntl_getlk_1(int fd, int host_cmd, abi_long arg,
                                 from_flock64_fn *copy_from,
                                 to_flock64_fn *copy_to)
{
    struct flock64 fl64;
    abi_long ret;

    ret = copy_from(&fl64, arg);
    if (ret == 0) {
        ret = get_errno(safe_fcntl(fd, host_cmd, &fl64));
        if (ret == 0) {
            ret = copy_to(arg, &fl64);
        }
    }
    return ret;
}

static abi_long do_fcntl_setlk_1(int fd, int host_cmd, abi_long arg,
                                 from_flock64_fn *copy_from)
{
    struct flock64 fl64;
    abi_long ret;

    ret = copy_from(&fl64, arg);
    if (ret == 0) {
        ret = get_errno(safe_fcntl(fd, host_cmd, &fl64));
    }
    return ret;
}

static abi_long do_fcntl_getlk(int fd, int cmd, abi_long arg)
{
    return do_fcntl_getlk_1(fd, cmd, arg,
                            copy_from_user_flock,
                            copy_to_user_flock);
}

static abi_long do_fcntl_setlk(int fd, int cmd, abi_long arg)
{
    return do_fcntl_setlk_1(fd, cmd, arg, copy_from_user_flock);
}

static abi_long do_fcntl_getlk64(int fd, int cmd, abi_long arg)
{
    return do_fcntl_getlk_1(fd, cmd, arg,
                            copy_from_user_flock64,
                            copy_to_user_flock64);
}

static abi_long do_fcntl_setlk64(int fd, int cmd, abi_long arg)
{
    return do_fcntl_setlk_1(fd, cmd, arg, copy_from_user_flock64);
}

#if defined(TARGET_ARM) && TARGET_ABI_BITS == 32
static abi_long do_fcntl_oabi_getlk64(int fd, int cmd, abi_long arg)
{
    return do_fcntl_getlk_1(fd, cmd, arg,
                            copy_from_user_oabi_flock64,
                            copy_to_user_oabi_flock64);
}

static abi_long do_fcntl_oabi_setlk64(int fd, int cmd, abi_long arg)
{
    return do_fcntl_setlk_1(fd, cmd, arg, copy_from_user_oabi_flock64);
}
#endif /* TARGET_ARM */

#ifdef F_GETOWN_EX
static abi_long do_fcntl_getown_ex(int fd, int cmd, abi_long arg)
{
    struct f_owner_ex fox;
    abi_long ret = get_errno(safe_fcntl(fd, cmd, &fox));

    if (!is_error(ret)) {
        struct target_f_owner_ex *target_fox;
        if (!lock_user_struct(VERIFY_WRITE, target_fox, arg, 0)) {
            return -TARGET_EFAULT;
        }
        target_fox->type = tswap32(fox.type);
        target_fox->pid = tswap32(fox.pid);
        unlock_user_struct(target_fox, arg, 1);
    }
    return ret;
}

static abi_long do_fcntl_setown_ex(int fd, int cmd, abi_long arg)
{
    struct target_f_owner_ex *target_fox;
    struct f_owner_ex fox;

    if (!lock_user_struct(VERIFY_READ, target_fox, arg, 1)) {
        return -TARGET_EFAULT;
    }
    fox.type = tswap32(target_fox->type);
    fox.pid = tswap32(target_fox->pid);
    unlock_user_struct(target_fox, arg, 0);
    return get_errno(safe_fcntl(fd, cmd, &fox));
}
#endif /* F_GETOWN_EX */

static const FcntlEntry *target_fcntl_cmd(int cmd, int is_64)
{
#define CMD2(T, H, A, FN)                                            \
    case TARGET_##T: do {                                            \
        static const FcntlEntry ent_##T = {                          \
            .name = #T, .host_cmd = H, .host_fn = FN, .arg_type = A  \
        };                                                           \
        return &ent_##T;                                             \
    } while(0)

#define CMD1(T, A, FN)                                               \
    case TARGET_##T: do {                                            \
        static const FcntlEntry ent_##T = {                          \
            .name = #T, .host_cmd = T, .host_fn = FN, .arg_type = A  \
        };                                                           \
        return &ent_##T;                                             \
    } while(0)

#if TARGET_ABI_BITS == 64
# ifdef __powerpc64__
/*
 * On PPC64, glibc headers has the F_*LK* defined to 12, 13 and 14 and
 * is not supported by kernel. The glibc fcntl call actually adjusts
 * them to 5, 6 and 7 before making the syscall(). Since we make the
 * syscall directly, adjust to what is supported by the kernel.
 */
#  define HOST_CMD_ADJ64(C)  (C - (F_GETLK64 - 5))
# else
#  define HOST_CMD_ADJ64(C)  C
# endif
# define CMD64(T, FN)                                                \
    case TARGET_##T: do {                                            \
        static const FcntlEntry ent_##T = {                          \
            .name = #T, .host_cmd = HOST_CMD_ADJ64(T),               \
            .host_fn = do_fcntl_##FN, .arg_type = ARG_PTR            \
        };                                                           \
        return &ent_##T;                                             \
    } while(0)
#elif defined(TARGET_ARM)
# define CMD64(T, FN)                                                \
    case TARGET_##T: do {                                            \
        if (!is_64) {                                                \
            return NULL;                                             \
        } else if (is_64 > 0) {                                      \
            static const FcntlEntry ent_##T = {                      \
                .name = #T, .host_cmd = T,                           \
                .host_fn = do_fcntl_##FN, .arg_type = ARG_PTR        \
            };                                                       \
            return &ent_##T;                                         \
        } else {                                                     \
            static const FcntlEntry ent_oabi_##T = {                 \
                .name = #T, .host_cmd = T,                           \
                .host_fn = do_fcntl_oabi_##FN, .arg_type = ARG_PTR   \
            };                                                       \
            return &ent_oabi_##T;                                    \
        }                                                            \
    } while (0)
#else
# define CMD64(T, FN)                                                \
    case TARGET_##T: do {                                            \
        static const FcntlEntry ent_##T = {                          \
            .name = #T, .host_cmd = T,                               \
            .host_fn = do_fcntl_##FN, .arg_type = ARG_PTR            \
        };                                                           \
        return is_64 ? &ent_##T : NULL;                              \
    } while (0)
#endif

    switch (cmd) {
    CMD1(F_DUPFD, ARG_DEC, do_fcntl_int);
    CMD1(F_GETFD, ARG_NONE, do_fcntl_int);
    CMD1(F_SETFD, ARG_DEC, do_fcntl_int);
    CMD1(F_GETFL, ARG_NONE, do_fcntl_getfl);
    CMD1(F_SETFL, ARG_DEC, do_fcntl_setfl);

    CMD2(F_GETLK, F_GETLK64, ARG_PTR, do_fcntl_getlk);
    CMD2(F_SETLK, F_SETLK64, ARG_PTR, do_fcntl_setlk);
    CMD2(F_SETLKW, F_SETLKW64, ARG_PTR, do_fcntl_setlk);

    CMD1(F_GETOWN, ARG_NONE, do_fcntl_int);
    CMD1(F_SETOWN, ARG_DEC, do_fcntl_int);
    CMD1(F_GETSIG, ARG_NONE, do_fcntl_int);
    CMD1(F_SETSIG, ARG_DEC, do_fcntl_int);

    CMD64(F_GETLK64, getlk64);
    CMD64(F_SETLK64, setlk64);
    CMD64(F_SETLKW64, setlk64);

    CMD1(F_GETLEASE, ARG_NONE, do_fcntl_int);
    CMD1(F_SETLEASE, ARG_DEC, do_fcntl_int);
#ifdef F_DUPFD_CLOEXEC
    CMD1(F_DUPFD_CLOEXEC, ARG_DEC, do_fcntl_int);
#endif
    CMD1(F_NOTIFY, ARG_DEC, do_fcntl_int);
#ifdef F_GETOWN_EX
    CMD1(F_GETOWN_EX, ARG_PTR, do_fcntl_getown_ex);
    CMD1(F_SETOWN_EX, ARG_PTR, do_fcntl_setown_ex);
#endif
#ifdef F_SETPIPE_SZ
    CMD1(F_SETPIPE_SZ, ARG_DEC, do_fcntl_int);
    CMD1(F_GETPIPE_SZ, ARG_DEC, do_fcntl_int);
#endif
    }
    return NULL;

#undef CMD1
#undef CMD2
#undef CMD64
#undef HOST_CMD_ADJ64
}

static abi_long do_fcntl(int fd, int target_cmd, abi_ulong arg, int is_64)
{
    const FcntlEntry *ent = target_fcntl_cmd(target_cmd, is_64);

    if (ent == NULL) {
        return -TARGET_EINVAL;
    }
    return ent->host_fn(fd, ent->host_cmd, arg);
}

static void do_print_fcntl(const SyscallDef *def, int fd, int target_cmd,
                           abi_ulong arg, int is_64)
{
    const FcntlEntry *ent = target_fcntl_cmd(target_cmd, is_64);

    switch (ent->arg_type) {
    case ARG_NONE:
        gemu_log("%d %s(%d,%s)", getpid(), def->name, fd, ent->name);
        break;
    case ARG_DEC:
        gemu_log("%d %s(%d,%s," TARGET_ABI_FMT_ld ")",
                 getpid(), def->name, fd, ent->name, arg);
        break;
    case ARG_PTR:
        gemu_log("%d %s(%d,%s,0x" TARGET_ABI_FMT_lx ")",
                 getpid(), def->name, fd, ent->name, arg);
        break;
    default:
        g_assert_not_reached();
    }
}

#ifdef TARGET_NR_fcntl
SYSCALL_IMPL(fcntl)
{
    return do_fcntl(arg1, arg2, arg3, 0);
}

static void print_fcntl(const SyscallDef *def, int64_t in[6])
{
    return do_print_fcntl(def, in[0], in[1], in[2], 0);
}
#endif

#if TARGET_ABI_BITS == 32
SYSCALL_IMPL(fcntl64)
{
    int is_64 = 1;
#ifdef TARGET_ARM
    if (!cpu_env->eabi) {
        is_64 = -1;
    }
#endif
    return do_fcntl(arg1, arg2, arg3, is_64);
}

static void print_fcntl64(const SyscallDef *def, int64_t in[6])
{
    return do_print_fcntl(def, in[0], in[1], in[2], 1);
}
#endif
