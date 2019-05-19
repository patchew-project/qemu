/*
 *  Linux syscalls
 *
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
#define _ATFILE_SOURCE
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include <elf.h>
#include <endian.h>
#include <grp.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/fsuid.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/swap.h>
#include <linux/capability.h>
#include <sched.h>
#include <sys/timex.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <poll.h>
#include <sys/times.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/statfs.h>
#include <utime.h>
#include <sys/sysinfo.h>
#include <sys/signalfd.h>
//#include <sys/user.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <linux/wireless.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/errqueue.h>
#include <linux/random.h>
#include "qemu-common.h"
#ifdef CONFIG_TIMERFD
#include <sys/timerfd.h>
#endif
#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif
#ifdef CONFIG_EPOLL
#include <sys/epoll.h>
#endif
#ifdef CONFIG_ATTR
#include "qemu/xattr.h"
#endif
#ifdef CONFIG_SENDFILE
#include <sys/sendfile.h>
#endif

#define termios host_termios
#define winsize host_winsize
#define termio host_termio
#define sgttyb host_sgttyb /* same as target */
#define tchars host_tchars /* same as target */
#define ltchars host_ltchars /* same as target */

#include <linux/termios.h>
#include <linux/unistd.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/soundcard.h>
#include <linux/kd.h>
#include <linux/mtio.h>
#include <linux/fs.h>
#if defined(CONFIG_FIEMAP)
#include <linux/fiemap.h>
#endif
#include <linux/fb.h>
#if defined(CONFIG_USBFS)
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#endif
#include <linux/vt.h>
#include <linux/dm-ioctl.h>
#include <linux/reboot.h>
#include <linux/route.h>
#include <linux/filter.h>
#include <linux/blkpg.h>
#include <netpacket/packet.h>
#include <linux/netlink.h>
#include "linux_loop.h"
#include "uname.h"

#include "qemu.h"
#include "fd-trans.h"
#include "syscall.h"

/* Define DEBUG_ERESTARTSYS to force every syscall to be restarted
 * once. This exercises the codepaths for restart.
 */
//#define DEBUG_ERESTARTSYS

//#include <linux/msdos_fs.h>
#define	VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct linux_dirent [2])
#define	VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct linux_dirent [2])

#undef _syscall0
#undef _syscall1
#undef _syscall2
#undef _syscall3
#undef _syscall4
#undef _syscall5
#undef _syscall6

#define _syscall0(type,name)		\
static type name (void)			\
{					\
	return syscall(__NR_##name);	\
}

#define _syscall1(type,name,type1,arg1)		\
static type name (type1 arg1)			\
{						\
	return syscall(__NR_##name, arg1);	\
}

#define _syscall2(type,name,type1,arg1,type2,arg2)	\
static type name (type1 arg1,type2 arg2)		\
{							\
	return syscall(__NR_##name, arg1, arg2);	\
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)	\
static type name (type1 arg1,type2 arg2,type3 arg3)		\
{								\
	return syscall(__NR_##name, arg1, arg2, arg3);		\
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4)	\
static type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4)			\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4);			\
}

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,	\
		  type5,arg5)							\
static type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5)	\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5);		\
}


#define _syscall6(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,	\
		  type5,arg5,type6,arg6)					\
static type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5,	\
                  type6 arg6)							\
{										\
	return syscall(__NR_##name, arg1, arg2, arg3, arg4, arg5, arg6);	\
}


#define __NR_sys_uname __NR_uname
#define __NR_sys_getcwd1 __NR_getcwd
#define __NR_sys_getdents __NR_getdents
#define __NR_sys_getdents64 __NR_getdents64
#define __NR_sys_getpriority __NR_getpriority
#define __NR_sys_rt_sigqueueinfo __NR_rt_sigqueueinfo
#define __NR_sys_rt_tgsigqueueinfo __NR_rt_tgsigqueueinfo
#define __NR_sys_syslog __NR_syslog
#define __NR_sys_futex __NR_futex
#define __NR_sys_inotify_init __NR_inotify_init
#define __NR_sys_inotify_add_watch __NR_inotify_add_watch
#define __NR_sys_inotify_rm_watch __NR_inotify_rm_watch

#if defined(__alpha__) || defined(__x86_64__) || defined(__s390x__)
#define __NR__llseek __NR_lseek
#endif

/* Newer kernel ports have llseek() instead of _llseek() */
#if !defined(TARGET_NR_llseek) && defined(TARGET_NR__llseek)
#define TARGET_NR_llseek TARGET_NR__llseek
#endif

#define __NR_sys_gettid __NR_gettid
_syscall0(int, sys_gettid)

/*
 * These definitions produce an ENOSYS from the host kernel.
 * Performing a bogus syscall is easier than boilerplating
 * the replacement functions here in C.
 */
#ifndef __NR_dup3
#define __NR_dup3  -1
#endif
#ifndef __NR_pipe2
#define __NR_pipe2  -1
#endif
#ifndef __NR_syncfs
#define __NR_syncfs  -1
#endif

/* For the 64-bit guest on 32-bit host case we must emulate
 * getdents using getdents64, because otherwise the host
 * might hand us back more dirent records than we can fit
 * into the guest buffer after structure format conversion.
 * Otherwise we emulate getdents with getdents if the host has it.
 */
#if defined(__NR_getdents) && HOST_LONG_BITS >= TARGET_ABI_BITS
#define EMULATE_GETDENTS_WITH_GETDENTS
#endif

#if defined(TARGET_NR_getdents) && defined(EMULATE_GETDENTS_WITH_GETDENTS)
_syscall3(int, sys_getdents, uint, fd, struct linux_dirent *, dirp, uint, count);
#endif
#if (defined(TARGET_NR_getdents) && \
      !defined(EMULATE_GETDENTS_WITH_GETDENTS)) || \
    (defined(TARGET_NR_getdents64) && defined(__NR_getdents64))
_syscall3(int, sys_getdents64, uint, fd, struct linux_dirent64 *, dirp, uint, count);
#endif
_syscall3(int, sys_rt_sigqueueinfo, pid_t, pid, int, sig, siginfo_t *, uinfo)
_syscall4(int, sys_rt_tgsigqueueinfo, pid_t, pid, pid_t, tid, int, sig,
          siginfo_t *, uinfo)
_syscall3(int,sys_syslog,int,type,char*,bufp,int,len)
#ifdef __NR_exit_group
_syscall1(int,exit_group,int,error_code)
#endif
#if defined(TARGET_NR_set_tid_address) && defined(__NR_set_tid_address)
_syscall1(int,set_tid_address,int *,tidptr)
#endif
#if defined(TARGET_NR_futex) && defined(__NR_futex)
_syscall6(int,sys_futex,int *,uaddr,int,op,int,val,
          const struct timespec *,timeout,int *,uaddr2,int,val3)
#endif
#define __NR_sys_sched_getaffinity __NR_sched_getaffinity
_syscall3(int, sys_sched_getaffinity, pid_t, pid, unsigned int, len,
          unsigned long *, user_mask_ptr);
#define __NR_sys_sched_setaffinity __NR_sched_setaffinity
_syscall3(int, sys_sched_setaffinity, pid_t, pid, unsigned int, len,
          unsigned long *, user_mask_ptr);
#define __NR_sys_getcpu __NR_getcpu
_syscall3(int, sys_getcpu, unsigned *, cpu, unsigned *, node, void *, tcache);
_syscall4(int, reboot, int, magic1, int, magic2, unsigned int, cmd,
          void *, arg);
_syscall2(int, capget, struct __user_cap_header_struct *, header,
          struct __user_cap_data_struct *, data);
_syscall2(int, capset, struct __user_cap_header_struct *, header,
          struct __user_cap_data_struct *, data);
#if defined(TARGET_NR_ioprio_get) && defined(__NR_ioprio_get)
_syscall2(int, ioprio_get, int, which, int, who)
#endif
#if defined(TARGET_NR_ioprio_set) && defined(__NR_ioprio_set)
_syscall3(int, ioprio_set, int, which, int, who, int, ioprio)
#endif
#if defined(TARGET_NR_getrandom) && defined(__NR_getrandom)
_syscall3(int, getrandom, void *, buf, size_t, buflen, unsigned int, flags)
#endif
#if defined(TARGET_NR_kcmp) && defined(__NR_kcmp)
_syscall5(int, kcmp, pid_t, pid1, pid_t, pid2, int, type,
          unsigned long, idx1, unsigned long, idx2)
#endif
#ifndef CONFIG_SYNCFS
_syscall1(int, syncfs, int, fd)
#endif
#ifndef CONFIG_PIPE2
static int pipe2(int *fds, int flags)
{
    if (flags) {
        return syscall(__NR_pipe2, fds, flags);
    } else {
        return pipe(fds);
    }
}
#endif

static bitmask_transtbl fcntl_flags_tbl[] = {
  { TARGET_O_ACCMODE,   TARGET_O_WRONLY,    O_ACCMODE,   O_WRONLY,    },
  { TARGET_O_ACCMODE,   TARGET_O_RDWR,      O_ACCMODE,   O_RDWR,      },
  { TARGET_O_CREAT,     TARGET_O_CREAT,     O_CREAT,     O_CREAT,     },
  { TARGET_O_EXCL,      TARGET_O_EXCL,      O_EXCL,      O_EXCL,      },
  { TARGET_O_NOCTTY,    TARGET_O_NOCTTY,    O_NOCTTY,    O_NOCTTY,    },
  { TARGET_O_TRUNC,     TARGET_O_TRUNC,     O_TRUNC,     O_TRUNC,     },
  { TARGET_O_APPEND,    TARGET_O_APPEND,    O_APPEND,    O_APPEND,    },
  { TARGET_O_NONBLOCK,  TARGET_O_NONBLOCK,  O_NONBLOCK,  O_NONBLOCK,  },
  { TARGET_O_SYNC,      TARGET_O_DSYNC,     O_SYNC,      O_DSYNC,     },
  { TARGET_O_SYNC,      TARGET_O_SYNC,      O_SYNC,      O_SYNC,      },
  { TARGET_FASYNC,      TARGET_FASYNC,      FASYNC,      FASYNC,      },
  { TARGET_O_DIRECTORY, TARGET_O_DIRECTORY, O_DIRECTORY, O_DIRECTORY, },
  { TARGET_O_NOFOLLOW,  TARGET_O_NOFOLLOW,  O_NOFOLLOW,  O_NOFOLLOW,  },
#if defined(O_DIRECT)
  { TARGET_O_DIRECT,    TARGET_O_DIRECT,    O_DIRECT,    O_DIRECT,    },
#endif
#if defined(O_NOATIME)
  { TARGET_O_NOATIME,   TARGET_O_NOATIME,   O_NOATIME,   O_NOATIME    },
#endif
#if defined(O_CLOEXEC)
  { TARGET_O_CLOEXEC,   TARGET_O_CLOEXEC,   O_CLOEXEC,   O_CLOEXEC    },
#endif
#if defined(O_PATH)
  { TARGET_O_PATH,      TARGET_O_PATH,      O_PATH,      O_PATH       },
#endif
#if defined(O_TMPFILE)
  { TARGET_O_TMPFILE,   TARGET_O_TMPFILE,   O_TMPFILE,   O_TMPFILE    },
#endif
  /* Don't terminate the list prematurely on 64-bit host+guest.  */
#if TARGET_O_LARGEFILE != 0 || O_LARGEFILE != 0
  { TARGET_O_LARGEFILE, TARGET_O_LARGEFILE, O_LARGEFILE, O_LARGEFILE, },
#endif
  { 0, 0, 0, 0 }
};

static int sys_getcwd1(char *buf, size_t size)
{
  if (getcwd(buf, size) == NULL) {
      /* getcwd() sets errno */
      return (-1);
  }
  return strlen(buf)+1;
}

#ifdef TARGET_NR_utimensat
#if defined(__NR_utimensat)
#define __NR_sys_utimensat __NR_utimensat
_syscall4(int,sys_utimensat,int,dirfd,const char *,pathname,
          const struct timespec *,tsp,int,flags)
#else
static int sys_utimensat(int dirfd, const char *pathname,
                         const struct timespec times[2], int flags)
{
    errno = ENOSYS;
    return -1;
}
#endif
#endif /* TARGET_NR_utimensat */

#ifdef CONFIG_INOTIFY
#include <sys/inotify.h>

#if defined(TARGET_NR_inotify_init) && defined(__NR_inotify_init)
static int sys_inotify_init(void)
{
  return (inotify_init());
}
#endif
#if defined(TARGET_NR_inotify_add_watch) && defined(__NR_inotify_add_watch)
static int sys_inotify_add_watch(int fd,const char *pathname, int32_t mask)
{
  return (inotify_add_watch(fd, pathname, mask));
}
#endif
#if defined(TARGET_NR_inotify_rm_watch) && defined(__NR_inotify_rm_watch)
static int sys_inotify_rm_watch(int fd, int32_t wd)
{
  return (inotify_rm_watch(fd, wd));
}
#endif
#ifdef CONFIG_INOTIFY1
#if defined(TARGET_NR_inotify_init1) && defined(__NR_inotify_init1)
static int sys_inotify_init1(int flags)
{
  return (inotify_init1(flags));
}
#endif
#endif
#else
/* Userspace can usually survive runtime without inotify */
#undef TARGET_NR_inotify_init
#undef TARGET_NR_inotify_init1
#undef TARGET_NR_inotify_add_watch
#undef TARGET_NR_inotify_rm_watch
#endif /* CONFIG_INOTIFY  */

#if defined(TARGET_NR_prlimit64)
#ifndef __NR_prlimit64
# define __NR_prlimit64 -1
#endif
#define __NR_sys_prlimit64 __NR_prlimit64
/* The glibc rlimit structure may not be that used by the underlying syscall */
struct host_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};
_syscall4(int, sys_prlimit64, pid_t, pid, int, resource,
          const struct host_rlimit64 *, new_limit,
          struct host_rlimit64 *, old_limit)
#endif


#if defined(TARGET_NR_timer_create)
/* Maxiumum of 32 active POSIX timers allowed at any one time. */
static timer_t g_posix_timers[32] = { 0, } ;

static inline int next_free_host_timer(void)
{
    int k ;
    /* FIXME: Does finding the next free slot require a lock? */
    for (k = 0; k < ARRAY_SIZE(g_posix_timers); k++) {
        if (g_posix_timers[k] == 0) {
            g_posix_timers[k] = (timer_t) 1;
            return k;
        }
    }
    return -1;
}
#endif

/*
 * Returns true if syscall NUM expects 64bit types aligned even
 * on pairs of registers.
 */
static inline bool regpairs_aligned(void *cpu_env, int num)
{
#ifdef TARGET_ARM
    return ((CPUARMState *)cpu_env)->eabi;
#elif defined(TARGET_MIPS) && TARGET_ABI_BITS == 32
    return true;
#elif defined(TARGET_PPC) && !defined(TARGET_PPC64)
    /*
     * SysV AVI for PPC32 expects 64bit parameters to be passed on
     * odd/even pairs of registers which translates to the same as
     * we start with r3 as arg1.
     */
    return true;
#elif defined(TARGET_SH4)
    /* SH4 doesn't align register pairs, except for p{read,write}64.  */
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

#define ERRNO_TABLE_SIZE 1200

/* target_to_host_errno_table[] is initialized from
 * host_to_target_errno_table[] in syscall_init(). */
static uint16_t target_to_host_errno_table[ERRNO_TABLE_SIZE] = {
};

/*
 * This list is the union of errno values overridden in asm-<arch>/errno.h
 * minus the errnos that are not actually generic to all archs.
 */
static uint16_t host_to_target_errno_table[ERRNO_TABLE_SIZE] = {
    [EAGAIN]		= TARGET_EAGAIN,
    [EIDRM]		= TARGET_EIDRM,
    [ECHRNG]		= TARGET_ECHRNG,
    [EL2NSYNC]		= TARGET_EL2NSYNC,
    [EL3HLT]		= TARGET_EL3HLT,
    [EL3RST]		= TARGET_EL3RST,
    [ELNRNG]		= TARGET_ELNRNG,
    [EUNATCH]		= TARGET_EUNATCH,
    [ENOCSI]		= TARGET_ENOCSI,
    [EL2HLT]		= TARGET_EL2HLT,
    [EDEADLK]		= TARGET_EDEADLK,
    [ENOLCK]		= TARGET_ENOLCK,
    [EBADE]		= TARGET_EBADE,
    [EBADR]		= TARGET_EBADR,
    [EXFULL]		= TARGET_EXFULL,
    [ENOANO]		= TARGET_ENOANO,
    [EBADRQC]		= TARGET_EBADRQC,
    [EBADSLT]		= TARGET_EBADSLT,
    [EBFONT]		= TARGET_EBFONT,
    [ENOSTR]		= TARGET_ENOSTR,
    [ENODATA]		= TARGET_ENODATA,
    [ETIME]		= TARGET_ETIME,
    [ENOSR]		= TARGET_ENOSR,
    [ENONET]		= TARGET_ENONET,
    [ENOPKG]		= TARGET_ENOPKG,
    [EREMOTE]		= TARGET_EREMOTE,
    [ENOLINK]		= TARGET_ENOLINK,
    [EADV]		= TARGET_EADV,
    [ESRMNT]		= TARGET_ESRMNT,
    [ECOMM]		= TARGET_ECOMM,
    [EPROTO]		= TARGET_EPROTO,
    [EDOTDOT]		= TARGET_EDOTDOT,
    [EMULTIHOP]		= TARGET_EMULTIHOP,
    [EBADMSG]		= TARGET_EBADMSG,
    [ENAMETOOLONG]	= TARGET_ENAMETOOLONG,
    [EOVERFLOW]		= TARGET_EOVERFLOW,
    [ENOTUNIQ]		= TARGET_ENOTUNIQ,
    [EBADFD]		= TARGET_EBADFD,
    [EREMCHG]		= TARGET_EREMCHG,
    [ELIBACC]		= TARGET_ELIBACC,
    [ELIBBAD]		= TARGET_ELIBBAD,
    [ELIBSCN]		= TARGET_ELIBSCN,
    [ELIBMAX]		= TARGET_ELIBMAX,
    [ELIBEXEC]		= TARGET_ELIBEXEC,
    [EILSEQ]		= TARGET_EILSEQ,
    [ENOSYS]		= TARGET_ENOSYS,
    [ELOOP]		= TARGET_ELOOP,
    [ERESTART]		= TARGET_ERESTART,
    [ESTRPIPE]		= TARGET_ESTRPIPE,
    [ENOTEMPTY]		= TARGET_ENOTEMPTY,
    [EUSERS]		= TARGET_EUSERS,
    [ENOTSOCK]		= TARGET_ENOTSOCK,
    [EDESTADDRREQ]	= TARGET_EDESTADDRREQ,
    [EMSGSIZE]		= TARGET_EMSGSIZE,
    [EPROTOTYPE]	= TARGET_EPROTOTYPE,
    [ENOPROTOOPT]	= TARGET_ENOPROTOOPT,
    [EPROTONOSUPPORT]	= TARGET_EPROTONOSUPPORT,
    [ESOCKTNOSUPPORT]	= TARGET_ESOCKTNOSUPPORT,
    [EOPNOTSUPP]	= TARGET_EOPNOTSUPP,
    [EPFNOSUPPORT]	= TARGET_EPFNOSUPPORT,
    [EAFNOSUPPORT]	= TARGET_EAFNOSUPPORT,
    [EADDRINUSE]	= TARGET_EADDRINUSE,
    [EADDRNOTAVAIL]	= TARGET_EADDRNOTAVAIL,
    [ENETDOWN]		= TARGET_ENETDOWN,
    [ENETUNREACH]	= TARGET_ENETUNREACH,
    [ENETRESET]		= TARGET_ENETRESET,
    [ECONNABORTED]	= TARGET_ECONNABORTED,
    [ECONNRESET]	= TARGET_ECONNRESET,
    [ENOBUFS]		= TARGET_ENOBUFS,
    [EISCONN]		= TARGET_EISCONN,
    [ENOTCONN]		= TARGET_ENOTCONN,
    [EUCLEAN]		= TARGET_EUCLEAN,
    [ENOTNAM]		= TARGET_ENOTNAM,
    [ENAVAIL]		= TARGET_ENAVAIL,
    [EISNAM]		= TARGET_EISNAM,
    [EREMOTEIO]		= TARGET_EREMOTEIO,
    [EDQUOT]            = TARGET_EDQUOT,
    [ESHUTDOWN]		= TARGET_ESHUTDOWN,
    [ETOOMANYREFS]	= TARGET_ETOOMANYREFS,
    [ETIMEDOUT]		= TARGET_ETIMEDOUT,
    [ECONNREFUSED]	= TARGET_ECONNREFUSED,
    [EHOSTDOWN]		= TARGET_EHOSTDOWN,
    [EHOSTUNREACH]	= TARGET_EHOSTUNREACH,
    [EALREADY]		= TARGET_EALREADY,
    [EINPROGRESS]	= TARGET_EINPROGRESS,
    [ESTALE]		= TARGET_ESTALE,
    [ECANCELED]		= TARGET_ECANCELED,
    [ENOMEDIUM]		= TARGET_ENOMEDIUM,
    [EMEDIUMTYPE]	= TARGET_EMEDIUMTYPE,
#ifdef ENOKEY
    [ENOKEY]		= TARGET_ENOKEY,
#endif
#ifdef EKEYEXPIRED
    [EKEYEXPIRED]	= TARGET_EKEYEXPIRED,
#endif
#ifdef EKEYREVOKED
    [EKEYREVOKED]	= TARGET_EKEYREVOKED,
#endif
#ifdef EKEYREJECTED
    [EKEYREJECTED]	= TARGET_EKEYREJECTED,
#endif
#ifdef EOWNERDEAD
    [EOWNERDEAD]	= TARGET_EOWNERDEAD,
#endif
#ifdef ENOTRECOVERABLE
    [ENOTRECOVERABLE]	= TARGET_ENOTRECOVERABLE,
#endif
#ifdef ENOMSG
    [ENOMSG]            = TARGET_ENOMSG,
#endif
#ifdef ERKFILL
    [ERFKILL]           = TARGET_ERFKILL,
#endif
#ifdef EHWPOISON
    [EHWPOISON]         = TARGET_EHWPOISON,
#endif
};

static inline int host_to_target_errno(int err)
{
    if (err >= 0 && err < ERRNO_TABLE_SIZE &&
        host_to_target_errno_table[err]) {
        return host_to_target_errno_table[err];
    }
    return err;
}

static inline int target_to_host_errno(int err)
{
    if (err >= 0 && err < ERRNO_TABLE_SIZE &&
        target_to_host_errno_table[err]) {
        return target_to_host_errno_table[err];
    }
    return err;
}

static inline abi_long get_errno(abi_long ret)
{
    if (ret == -1)
        return -host_to_target_errno(errno);
    else
        return ret;
}

const char *target_strerror(int err)
{
    if (err == TARGET_ERESTARTSYS) {
        return "To be restarted";
    }
    if (err == TARGET_QEMU_ESIGRETURN) {
        return "Successful exit from sigreturn";
    }

    if ((err >= ERRNO_TABLE_SIZE) || (err < 0)) {
        return NULL;
    }
    return strerror(target_to_host_errno(err));
}

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

safe_syscall3(ssize_t, read, int, fd, void *, buff, size_t, count)
safe_syscall3(ssize_t, write, int, fd, const void *, buff, size_t, count)
safe_syscall4(int, openat, int, dirfd, const char *, pathname, \
              int, flags, mode_t, mode)
safe_syscall4(pid_t, wait4, pid_t, pid, int *, status, int, options, \
              struct rusage *, rusage)
safe_syscall5(int, waitid, idtype_t, idtype, id_t, id, siginfo_t *, infop, \
              int, options, struct rusage *, rusage)
safe_syscall5(int, execveat, int, dirfd, const char *, filename,
              char **, argv, char **, envp, int, flags)
safe_syscall6(int, pselect6, int, nfds, fd_set *, readfds, fd_set *, writefds, \
              fd_set *, exceptfds, struct timespec *, timeout, void *, sig)
safe_syscall5(int, ppoll, struct pollfd *, ufds, unsigned int, nfds,
              struct timespec *, tsp, const sigset_t *, sigmask,
              size_t, sigsetsize)
safe_syscall6(int, epoll_pwait, int, epfd, struct epoll_event *, events,
              int, maxevents, int, timeout, const sigset_t *, sigmask,
              size_t, sigsetsize)
safe_syscall6(int,futex,int *,uaddr,int,op,int,val, \
              const struct timespec *,timeout,int *,uaddr2,int,val3)
safe_syscall2(int, rt_sigsuspend, sigset_t *, newset, size_t, sigsetsize)
safe_syscall2(int, kill, pid_t, pid, int, sig)
safe_syscall2(int, tkill, int, tid, int, sig)
safe_syscall3(int, tgkill, int, tgid, int, pid, int, sig)
safe_syscall3(ssize_t, readv, int, fd, const struct iovec *, iov, int, iovcnt)
safe_syscall3(ssize_t, writev, int, fd, const struct iovec *, iov, int, iovcnt)
safe_syscall5(ssize_t, preadv, int, fd, const struct iovec *, iov, int, iovcnt,
              unsigned long, pos_l, unsigned long, pos_h)
safe_syscall5(ssize_t, pwritev, int, fd, const struct iovec *, iov, int, iovcnt,
              unsigned long, pos_l, unsigned long, pos_h)
safe_syscall3(int, connect, int, fd, const struct sockaddr *, addr,
              socklen_t, addrlen)
safe_syscall6(ssize_t, sendto, int, fd, const void *, buf, size_t, len,
              int, flags, const struct sockaddr *, addr, socklen_t, addrlen)
safe_syscall6(ssize_t, recvfrom, int, fd, void *, buf, size_t, len,
              int, flags, struct sockaddr *, addr, socklen_t *, addrlen)
safe_syscall3(ssize_t, sendmsg, int, fd, const struct msghdr *, msg, int, flags)
safe_syscall3(ssize_t, recvmsg, int, fd, struct msghdr *, msg, int, flags)
safe_syscall2(int, flock, int, fd, int, operation)
safe_syscall4(int, rt_sigtimedwait, const sigset_t *, these, siginfo_t *, uinfo,
              const struct timespec *, uts, size_t, sigsetsize)
safe_syscall4(int, accept4, int, fd, struct sockaddr *, addr, socklen_t *, len,
              int, flags)
safe_syscall2(int, nanosleep, const struct timespec *, req,
              struct timespec *, rem)
#ifdef TARGET_NR_clock_nanosleep
safe_syscall4(int, clock_nanosleep, const clockid_t, clock, int, flags,
              const struct timespec *, req, struct timespec *, rem)
#endif
#if defined(TARGET_NR_mq_open) && defined(__NR_mq_open)
safe_syscall5(int, mq_timedsend, int, mqdes, const char *, msg_ptr,
              size_t, len, unsigned, prio, const struct timespec *, timeout)
safe_syscall5(int, mq_timedreceive, int, mqdes, char *, msg_ptr,
              size_t, len, unsigned *, prio, const struct timespec *, timeout)
#endif
safe_syscall5(int, name_to_handle_at, int, dirfd, const char *, pathname,
              struct file_handle *, handle, int *, mount_id, int, flags)
safe_syscall3(int, open_by_handle_at, int, mount_fd,
              struct file_handle *, handle, int, flags)
/* We do ioctl like this rather than via safe_syscall3 to preserve the
 * "third argument might be integer or pointer or not present" behaviour of
 * the libc function.
 */
#define safe_ioctl(...) safe_syscall(__NR_ioctl, __VA_ARGS__)
/* Similarly for fcntl. Note that callers must always:
 *  pass the F_GETLK64 etc constants rather than the unsuffixed F_GETLK
 *  use the flock64 struct rather than unsuffixed flock
 * This will then work and use a 64-bit offset for both 32-bit and 64-bit hosts.
 */
#ifdef __NR_fcntl64
#define safe_fcntl(...) safe_syscall(__NR_fcntl64, __VA_ARGS__)
#else
#define safe_fcntl(...) safe_syscall(__NR_fcntl, __VA_ARGS__)
#endif

static inline int host_to_target_sock_type(int host_type)
{
    int target_type;

    switch (host_type & 0xf /* SOCK_TYPE_MASK */) {
    case SOCK_DGRAM:
        target_type = TARGET_SOCK_DGRAM;
        break;
    case SOCK_STREAM:
        target_type = TARGET_SOCK_STREAM;
        break;
    default:
        target_type = host_type & 0xf /* SOCK_TYPE_MASK */;
        break;
    }

#if defined(SOCK_CLOEXEC)
    if (host_type & SOCK_CLOEXEC) {
        target_type |= TARGET_SOCK_CLOEXEC;
    }
#endif

#if defined(SOCK_NONBLOCK)
    if (host_type & SOCK_NONBLOCK) {
        target_type |= TARGET_SOCK_NONBLOCK;
    }
#endif

    return target_type;
}

static inline abi_long copy_from_user_fdset(fd_set *fds,
                                            abi_ulong target_fds_addr,
                                            int n)
{
    int i, nw, j, k;
    abi_ulong b, *target_fds;

    nw = DIV_ROUND_UP(n, TARGET_ABI_BITS);
    if (!(target_fds = lock_user(VERIFY_READ,
                                 target_fds_addr,
                                 sizeof(abi_ulong) * nw,
                                 1)))
        return -TARGET_EFAULT;

    FD_ZERO(fds);
    k = 0;
    for (i = 0; i < nw; i++) {
        /* grab the abi_ulong */
        __get_user(b, &target_fds[i]);
        for (j = 0; j < TARGET_ABI_BITS; j++) {
            /* check the bit inside the abi_ulong */
            if ((b >> j) & 1)
                FD_SET(k, fds);
            k++;
        }
    }

    unlock_user(target_fds, target_fds_addr, 0);

    return 0;
}

static inline abi_ulong copy_from_user_fdset_ptr(fd_set *fds, fd_set **fds_ptr,
                                                 abi_ulong target_fds_addr,
                                                 int n)
{
    if (target_fds_addr) {
        if (copy_from_user_fdset(fds, target_fds_addr, n))
            return -TARGET_EFAULT;
        *fds_ptr = fds;
    } else {
        *fds_ptr = NULL;
    }
    return 0;
}

static inline abi_long copy_to_user_fdset(abi_ulong target_fds_addr,
                                          const fd_set *fds,
                                          int n)
{
    int i, nw, j, k;
    abi_long v;
    abi_ulong *target_fds;

    nw = DIV_ROUND_UP(n, TARGET_ABI_BITS);
    if (!(target_fds = lock_user(VERIFY_WRITE,
                                 target_fds_addr,
                                 sizeof(abi_ulong) * nw,
                                 0)))
        return -TARGET_EFAULT;

    k = 0;
    for (i = 0; i < nw; i++) {
        v = 0;
        for (j = 0; j < TARGET_ABI_BITS; j++) {
            v |= ((abi_ulong)(FD_ISSET(k, fds) != 0) << j);
            k++;
        }
        __put_user(v, &target_fds[i]);
    }

    unlock_user(target_fds, target_fds_addr, sizeof(abi_ulong) * nw);

    return 0;
}

#if defined(__alpha__)
#define HOST_HZ 1024
#else
#define HOST_HZ 100
#endif

static inline abi_long host_to_target_clock_t(long ticks)
{
#if HOST_HZ == TARGET_HZ
    return ticks;
#else
    return ((int64_t)ticks * TARGET_HZ) / HOST_HZ;
#endif
}

static inline abi_long host_to_target_rusage(abi_ulong target_addr,
                                             const struct rusage *rusage)
{
    struct target_rusage *target_rusage;

    if (!lock_user_struct(VERIFY_WRITE, target_rusage, target_addr, 0))
        return -TARGET_EFAULT;
    target_rusage->ru_utime.tv_sec = tswapal(rusage->ru_utime.tv_sec);
    target_rusage->ru_utime.tv_usec = tswapal(rusage->ru_utime.tv_usec);
    target_rusage->ru_stime.tv_sec = tswapal(rusage->ru_stime.tv_sec);
    target_rusage->ru_stime.tv_usec = tswapal(rusage->ru_stime.tv_usec);
    target_rusage->ru_maxrss = tswapal(rusage->ru_maxrss);
    target_rusage->ru_ixrss = tswapal(rusage->ru_ixrss);
    target_rusage->ru_idrss = tswapal(rusage->ru_idrss);
    target_rusage->ru_isrss = tswapal(rusage->ru_isrss);
    target_rusage->ru_minflt = tswapal(rusage->ru_minflt);
    target_rusage->ru_majflt = tswapal(rusage->ru_majflt);
    target_rusage->ru_nswap = tswapal(rusage->ru_nswap);
    target_rusage->ru_inblock = tswapal(rusage->ru_inblock);
    target_rusage->ru_oublock = tswapal(rusage->ru_oublock);
    target_rusage->ru_msgsnd = tswapal(rusage->ru_msgsnd);
    target_rusage->ru_msgrcv = tswapal(rusage->ru_msgrcv);
    target_rusage->ru_nsignals = tswapal(rusage->ru_nsignals);
    target_rusage->ru_nvcsw = tswapal(rusage->ru_nvcsw);
    target_rusage->ru_nivcsw = tswapal(rusage->ru_nivcsw);
    unlock_user_struct(target_rusage, target_addr, 1);

    return 0;
}

static inline rlim_t target_to_host_rlim(abi_ulong target_rlim)
{
    abi_ulong target_rlim_swap;
    rlim_t result;
    
    target_rlim_swap = tswapal(target_rlim);
    if (target_rlim_swap == TARGET_RLIM_INFINITY)
        return RLIM_INFINITY;

    result = target_rlim_swap;
    if (target_rlim_swap != (rlim_t)result)
        return RLIM_INFINITY;
    
    return result;
}

static inline abi_ulong host_to_target_rlim(rlim_t rlim)
{
    abi_ulong target_rlim_swap;
    abi_ulong result;
    
    if (rlim == RLIM_INFINITY || rlim != (abi_long)rlim)
        target_rlim_swap = TARGET_RLIM_INFINITY;
    else
        target_rlim_swap = rlim;
    result = tswapal(target_rlim_swap);
    
    return result;
}

static inline int target_to_host_resource(int code)
{
    switch (code) {
    case TARGET_RLIMIT_AS:
        return RLIMIT_AS;
    case TARGET_RLIMIT_CORE:
        return RLIMIT_CORE;
    case TARGET_RLIMIT_CPU:
        return RLIMIT_CPU;
    case TARGET_RLIMIT_DATA:
        return RLIMIT_DATA;
    case TARGET_RLIMIT_FSIZE:
        return RLIMIT_FSIZE;
    case TARGET_RLIMIT_LOCKS:
        return RLIMIT_LOCKS;
    case TARGET_RLIMIT_MEMLOCK:
        return RLIMIT_MEMLOCK;
    case TARGET_RLIMIT_MSGQUEUE:
        return RLIMIT_MSGQUEUE;
    case TARGET_RLIMIT_NICE:
        return RLIMIT_NICE;
    case TARGET_RLIMIT_NOFILE:
        return RLIMIT_NOFILE;
    case TARGET_RLIMIT_NPROC:
        return RLIMIT_NPROC;
    case TARGET_RLIMIT_RSS:
        return RLIMIT_RSS;
    case TARGET_RLIMIT_RTPRIO:
        return RLIMIT_RTPRIO;
    case TARGET_RLIMIT_SIGPENDING:
        return RLIMIT_SIGPENDING;
    case TARGET_RLIMIT_STACK:
        return RLIMIT_STACK;
    default:
        return code;
    }
}

static inline abi_long copy_from_user_timeval(struct timeval *tv,
                                              abi_ulong target_tv_addr)
{
    struct target_timeval *target_tv;

    if (!lock_user_struct(VERIFY_READ, target_tv, target_tv_addr, 1))
        return -TARGET_EFAULT;

    __get_user(tv->tv_sec, &target_tv->tv_sec);
    __get_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 0);

    return 0;
}

static inline abi_long copy_to_user_timeval(abi_ulong target_tv_addr,
                                            const struct timeval *tv)
{
    struct target_timeval *target_tv;

    if (!lock_user_struct(VERIFY_WRITE, target_tv, target_tv_addr, 0))
        return -TARGET_EFAULT;

    __put_user(tv->tv_sec, &target_tv->tv_sec);
    __put_user(tv->tv_usec, &target_tv->tv_usec);

    unlock_user_struct(target_tv, target_tv_addr, 1);

    return 0;
}

static inline abi_long copy_from_user_timezone(struct timezone *tz,
                                               abi_ulong target_tz_addr)
{
    struct target_timezone *target_tz;

    if (!lock_user_struct(VERIFY_READ, target_tz, target_tz_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(tz->tz_minuteswest, &target_tz->tz_minuteswest);
    __get_user(tz->tz_dsttime, &target_tz->tz_dsttime);

    unlock_user_struct(target_tz, target_tz_addr, 0);

    return 0;
}

#if defined(TARGET_NR_mq_open) && defined(__NR_mq_open)
#include <mqueue.h>

static inline abi_long copy_from_user_mq_attr(struct mq_attr *attr,
                                              abi_ulong target_mq_attr_addr)
{
    struct target_mq_attr *target_mq_attr;

    if (!lock_user_struct(VERIFY_READ, target_mq_attr,
                          target_mq_attr_addr, 1))
        return -TARGET_EFAULT;

    __get_user(attr->mq_flags, &target_mq_attr->mq_flags);
    __get_user(attr->mq_maxmsg, &target_mq_attr->mq_maxmsg);
    __get_user(attr->mq_msgsize, &target_mq_attr->mq_msgsize);
    __get_user(attr->mq_curmsgs, &target_mq_attr->mq_curmsgs);

    unlock_user_struct(target_mq_attr, target_mq_attr_addr, 0);

    return 0;
}

static inline abi_long copy_to_user_mq_attr(abi_ulong target_mq_attr_addr,
                                            const struct mq_attr *attr)
{
    struct target_mq_attr *target_mq_attr;

    if (!lock_user_struct(VERIFY_WRITE, target_mq_attr,
                          target_mq_attr_addr, 0))
        return -TARGET_EFAULT;

    __put_user(attr->mq_flags, &target_mq_attr->mq_flags);
    __put_user(attr->mq_maxmsg, &target_mq_attr->mq_maxmsg);
    __put_user(attr->mq_msgsize, &target_mq_attr->mq_msgsize);
    __put_user(attr->mq_curmsgs, &target_mq_attr->mq_curmsgs);

    unlock_user_struct(target_mq_attr, target_mq_attr_addr, 1);

    return 0;
}
#endif

#if defined(TARGET_NR_select) || defined(TARGET_NR__newselect)
/* do_select() must return target values and target errnos. */
static abi_long do_select(int n,
                          abi_ulong rfd_addr, abi_ulong wfd_addr,
                          abi_ulong efd_addr, abi_ulong target_tv_addr)
{
    fd_set rfds, wfds, efds;
    fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
    struct timeval tv;
    struct timespec ts, *ts_ptr;
    abi_long ret;

    ret = copy_from_user_fdset_ptr(&rfds, &rfds_ptr, rfd_addr, n);
    if (ret) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&wfds, &wfds_ptr, wfd_addr, n);
    if (ret) {
        return ret;
    }
    ret = copy_from_user_fdset_ptr(&efds, &efds_ptr, efd_addr, n);
    if (ret) {
        return ret;
    }

    if (target_tv_addr) {
        if (copy_from_user_timeval(&tv, target_tv_addr))
            return -TARGET_EFAULT;
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;
        ts_ptr = &ts;
    } else {
        ts_ptr = NULL;
    }

    ret = get_errno(safe_pselect6(n, rfds_ptr, wfds_ptr, efds_ptr,
                                  ts_ptr, NULL));

    if (!is_error(ret)) {
        if (rfd_addr && copy_to_user_fdset(rfd_addr, &rfds, n))
            return -TARGET_EFAULT;
        if (wfd_addr && copy_to_user_fdset(wfd_addr, &wfds, n))
            return -TARGET_EFAULT;
        if (efd_addr && copy_to_user_fdset(efd_addr, &efds, n))
            return -TARGET_EFAULT;

        if (target_tv_addr) {
            tv.tv_sec = ts.tv_sec;
            tv.tv_usec = ts.tv_nsec / 1000;
            if (copy_to_user_timeval(target_tv_addr, &tv)) {
                return -TARGET_EFAULT;
            }
        }
    }

    return ret;
}

#if defined(TARGET_WANT_OLD_SYS_SELECT)
static abi_long do_old_select(abi_ulong arg1)
{
    struct target_sel_arg_struct *sel;
    abi_ulong inp, outp, exp, tvp;
    long nsel;

    if (!lock_user_struct(VERIFY_READ, sel, arg1, 1)) {
        return -TARGET_EFAULT;
    }

    nsel = tswapal(sel->n);
    inp = tswapal(sel->inp);
    outp = tswapal(sel->outp);
    exp = tswapal(sel->exp);
    tvp = tswapal(sel->tvp);

    unlock_user_struct(sel, arg1, 0);

    return do_select(nsel, inp, outp, exp, tvp);
}
#endif
#endif

static inline abi_long target_to_host_ip_mreq(struct ip_mreqn *mreqn,
                                              abi_ulong target_addr,
                                              socklen_t len)
{
    struct target_ip_mreqn *target_smreqn;

    target_smreqn = lock_user(VERIFY_READ, target_addr, len, 1);
    if (!target_smreqn)
        return -TARGET_EFAULT;
    mreqn->imr_multiaddr.s_addr = target_smreqn->imr_multiaddr.s_addr;
    mreqn->imr_address.s_addr = target_smreqn->imr_address.s_addr;
    if (len == sizeof(struct target_ip_mreqn))
        mreqn->imr_ifindex = tswapal(target_smreqn->imr_ifindex);
    unlock_user(target_smreqn, target_addr, 0);

    return 0;
}

static inline abi_long target_to_host_sockaddr(int fd, struct sockaddr *addr,
                                               abi_ulong target_addr,
                                               socklen_t len)
{
    const socklen_t unix_maxlen = sizeof (struct sockaddr_un);
    sa_family_t sa_family;
    struct target_sockaddr *target_saddr;

    if (fd_trans_target_to_host_addr(fd)) {
        return fd_trans_target_to_host_addr(fd)(addr, target_addr, len);
    }

    target_saddr = lock_user(VERIFY_READ, target_addr, len, 1);
    if (!target_saddr)
        return -TARGET_EFAULT;

    sa_family = tswap16(target_saddr->sa_family);

    /* Oops. The caller might send a incomplete sun_path; sun_path
     * must be terminated by \0 (see the manual page), but
     * unfortunately it is quite common to specify sockaddr_un
     * length as "strlen(x->sun_path)" while it should be
     * "strlen(...) + 1". We'll fix that here if needed.
     * Linux kernel has a similar feature.
     */

    if (sa_family == AF_UNIX) {
        if (len < unix_maxlen && len > 0) {
            char *cp = (char*)target_saddr;

            if ( cp[len-1] && !cp[len] )
                len++;
        }
        if (len > unix_maxlen)
            len = unix_maxlen;
    }

    memcpy(addr, target_saddr, len);
    addr->sa_family = sa_family;
    if (sa_family == AF_NETLINK) {
        struct sockaddr_nl *nladdr;

        nladdr = (struct sockaddr_nl *)addr;
        nladdr->nl_pid = tswap32(nladdr->nl_pid);
        nladdr->nl_groups = tswap32(nladdr->nl_groups);
    } else if (sa_family == AF_PACKET) {
	struct target_sockaddr_ll *lladdr;

	lladdr = (struct target_sockaddr_ll *)addr;
	lladdr->sll_ifindex = tswap32(lladdr->sll_ifindex);
	lladdr->sll_hatype = tswap16(lladdr->sll_hatype);
    }
    unlock_user(target_saddr, target_addr, 0);

    return 0;
}

static inline abi_long host_to_target_sockaddr(abi_ulong target_addr,
                                               struct sockaddr *addr,
                                               socklen_t len)
{
    struct target_sockaddr *target_saddr;

    if (len == 0) {
        return 0;
    }
    assert(addr);

    target_saddr = lock_user(VERIFY_WRITE, target_addr, len, 0);
    if (!target_saddr)
        return -TARGET_EFAULT;
    memcpy(target_saddr, addr, len);
    if (len >= offsetof(struct target_sockaddr, sa_family) +
        sizeof(target_saddr->sa_family)) {
        target_saddr->sa_family = tswap16(addr->sa_family);
    }
    if (addr->sa_family == AF_NETLINK && len >= sizeof(struct sockaddr_nl)) {
        struct sockaddr_nl *target_nl = (struct sockaddr_nl *)target_saddr;
        target_nl->nl_pid = tswap32(target_nl->nl_pid);
        target_nl->nl_groups = tswap32(target_nl->nl_groups);
    } else if (addr->sa_family == AF_PACKET) {
        struct sockaddr_ll *target_ll = (struct sockaddr_ll *)target_saddr;
        target_ll->sll_ifindex = tswap32(target_ll->sll_ifindex);
        target_ll->sll_hatype = tswap16(target_ll->sll_hatype);
    } else if (addr->sa_family == AF_INET6 &&
               len >= sizeof(struct target_sockaddr_in6)) {
        struct target_sockaddr_in6 *target_in6 =
               (struct target_sockaddr_in6 *)target_saddr;
        target_in6->sin6_scope_id = tswap16(target_in6->sin6_scope_id);
    }
    unlock_user(target_saddr, target_addr, len);

    return 0;
}

static inline abi_long target_to_host_cmsg(struct msghdr *msgh,
                                           struct target_msghdr *target_msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg, *target_cmsg_start;
    socklen_t space = 0;
    
    msg_controllen = tswapal(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_READ, target_cmsg_addr, msg_controllen, 1);
    target_cmsg_start = target_cmsg;
    if (!target_cmsg)
        return -TARGET_EFAULT;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = tswapal(target_cmsg->cmsg_len)
            - sizeof(struct target_cmsghdr);

        space += CMSG_SPACE(len);
        if (space > msgh->msg_controllen) {
            space -= CMSG_SPACE(len);
            /* This is a QEMU bug, since we allocated the payload
             * area ourselves (unlike overflow in host-to-target
             * conversion, which is just the guest giving us a buffer
             * that's too small). It can't happen for the payload types
             * we currently support; if it becomes an issue in future
             * we would need to improve our allocation strategy to
             * something more intelligent than "twice the size of the
             * target buffer we're reading from".
             */
            gemu_log("Host cmsg overflow\n");
            break;
        }

        if (tswap32(target_cmsg->cmsg_level) == TARGET_SOL_SOCKET) {
            cmsg->cmsg_level = SOL_SOCKET;
        } else {
            cmsg->cmsg_level = tswap32(target_cmsg->cmsg_level);
        }
        cmsg->cmsg_type = tswap32(target_cmsg->cmsg_type);
        cmsg->cmsg_len = CMSG_LEN(len);

        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            int *fd = (int *)data;
            int *target_fd = (int *)target_data;
            int i, numfds = len / sizeof(int);

            for (i = 0; i < numfds; i++) {
                __get_user(fd[i], target_fd + i);
            }
        } else if (cmsg->cmsg_level == SOL_SOCKET
               &&  cmsg->cmsg_type == SCM_CREDENTIALS) {
            struct ucred *cred = (struct ucred *)data;
            struct target_ucred *target_cred =
                (struct target_ucred *)target_data;

            __get_user(cred->pid, &target_cred->pid);
            __get_user(cred->uid, &target_cred->uid);
            __get_user(cred->gid, &target_cred->gid);
        } else {
            gemu_log("Unsupported ancillary data: %d/%d\n",
                                        cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(data, target_data, len);
        }

        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg,
                                         target_cmsg_start);
    }
    unlock_user(target_cmsg, target_cmsg_addr, 0);
 the_end:
    msgh->msg_controllen = space;
    return 0;
}

static inline abi_long host_to_target_cmsg(struct target_msghdr *target_msgh,
                                           struct msghdr *msgh)
{
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msgh);
    abi_long msg_controllen;
    abi_ulong target_cmsg_addr;
    struct target_cmsghdr *target_cmsg, *target_cmsg_start;
    socklen_t space = 0;

    msg_controllen = tswapal(target_msgh->msg_controllen);
    if (msg_controllen < sizeof (struct target_cmsghdr)) 
        goto the_end;
    target_cmsg_addr = tswapal(target_msgh->msg_control);
    target_cmsg = lock_user(VERIFY_WRITE, target_cmsg_addr, msg_controllen, 0);
    target_cmsg_start = target_cmsg;
    if (!target_cmsg)
        return -TARGET_EFAULT;

    while (cmsg && target_cmsg) {
        void *data = CMSG_DATA(cmsg);
        void *target_data = TARGET_CMSG_DATA(target_cmsg);

        int len = cmsg->cmsg_len - sizeof(struct cmsghdr);
        int tgt_len, tgt_space;

        /* We never copy a half-header but may copy half-data;
         * this is Linux's behaviour in put_cmsg(). Note that
         * truncation here is a guest problem (which we report
         * to the guest via the CTRUNC bit), unlike truncation
         * in target_to_host_cmsg, which is a QEMU bug.
         */
        if (msg_controllen < sizeof(struct target_cmsghdr)) {
            target_msgh->msg_flags |= tswap32(MSG_CTRUNC);
            break;
        }

        if (cmsg->cmsg_level == SOL_SOCKET) {
            target_cmsg->cmsg_level = tswap32(TARGET_SOL_SOCKET);
        } else {
            target_cmsg->cmsg_level = tswap32(cmsg->cmsg_level);
        }
        target_cmsg->cmsg_type = tswap32(cmsg->cmsg_type);

        /* Payload types which need a different size of payload on
         * the target must adjust tgt_len here.
         */
        tgt_len = len;
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SO_TIMESTAMP:
                tgt_len = sizeof(struct target_timeval);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }

        if (msg_controllen < TARGET_CMSG_LEN(tgt_len)) {
            target_msgh->msg_flags |= tswap32(MSG_CTRUNC);
            tgt_len = msg_controllen - sizeof(struct target_cmsghdr);
        }

        /* We must now copy-and-convert len bytes of payload
         * into tgt_len bytes of destination space. Bear in mind
         * that in both source and destination we may be dealing
         * with a truncated value!
         */
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            switch (cmsg->cmsg_type) {
            case SCM_RIGHTS:
            {
                int *fd = (int *)data;
                int *target_fd = (int *)target_data;
                int i, numfds = tgt_len / sizeof(int);

                for (i = 0; i < numfds; i++) {
                    __put_user(fd[i], target_fd + i);
                }
                break;
            }
            case SO_TIMESTAMP:
            {
                struct timeval *tv = (struct timeval *)data;
                struct target_timeval *target_tv =
                    (struct target_timeval *)target_data;

                if (len != sizeof(struct timeval) ||
                    tgt_len != sizeof(struct target_timeval)) {
                    goto unimplemented;
                }

                /* copy struct timeval to target */
                __put_user(tv->tv_sec, &target_tv->tv_sec);
                __put_user(tv->tv_usec, &target_tv->tv_usec);
                break;
            }
            case SCM_CREDENTIALS:
            {
                struct ucred *cred = (struct ucred *)data;
                struct target_ucred *target_cred =
                    (struct target_ucred *)target_data;

                __put_user(cred->pid, &target_cred->pid);
                __put_user(cred->uid, &target_cred->uid);
                __put_user(cred->gid, &target_cred->gid);
                break;
            }
            default:
                goto unimplemented;
            }
            break;

        case SOL_IP:
            switch (cmsg->cmsg_type) {
            case IP_TTL:
            {
                uint32_t *v = (uint32_t *)data;
                uint32_t *t_int = (uint32_t *)target_data;

                if (len != sizeof(uint32_t) ||
                    tgt_len != sizeof(uint32_t)) {
                    goto unimplemented;
                }
                __put_user(*v, t_int);
                break;
            }
            case IP_RECVERR:
            {
                struct errhdr_t {
                   struct sock_extended_err ee;
                   struct sockaddr_in offender;
                };
                struct errhdr_t *errh = (struct errhdr_t *)data;
                struct errhdr_t *target_errh =
                    (struct errhdr_t *)target_data;

                if (len != sizeof(struct errhdr_t) ||
                    tgt_len != sizeof(struct errhdr_t)) {
                    goto unimplemented;
                }
                __put_user(errh->ee.ee_errno, &target_errh->ee.ee_errno);
                __put_user(errh->ee.ee_origin, &target_errh->ee.ee_origin);
                __put_user(errh->ee.ee_type,  &target_errh->ee.ee_type);
                __put_user(errh->ee.ee_code, &target_errh->ee.ee_code);
                __put_user(errh->ee.ee_pad, &target_errh->ee.ee_pad);
                __put_user(errh->ee.ee_info, &target_errh->ee.ee_info);
                __put_user(errh->ee.ee_data, &target_errh->ee.ee_data);
                host_to_target_sockaddr((unsigned long) &target_errh->offender,
                    (void *) &errh->offender, sizeof(errh->offender));
                break;
            }
            default:
                goto unimplemented;
            }
            break;

        case SOL_IPV6:
            switch (cmsg->cmsg_type) {
            case IPV6_HOPLIMIT:
            {
                uint32_t *v = (uint32_t *)data;
                uint32_t *t_int = (uint32_t *)target_data;

                if (len != sizeof(uint32_t) ||
                    tgt_len != sizeof(uint32_t)) {
                    goto unimplemented;
                }
                __put_user(*v, t_int);
                break;
            }
            case IPV6_RECVERR:
            {
                struct errhdr6_t {
                   struct sock_extended_err ee;
                   struct sockaddr_in6 offender;
                };
                struct errhdr6_t *errh = (struct errhdr6_t *)data;
                struct errhdr6_t *target_errh =
                    (struct errhdr6_t *)target_data;

                if (len != sizeof(struct errhdr6_t) ||
                    tgt_len != sizeof(struct errhdr6_t)) {
                    goto unimplemented;
                }
                __put_user(errh->ee.ee_errno, &target_errh->ee.ee_errno);
                __put_user(errh->ee.ee_origin, &target_errh->ee.ee_origin);
                __put_user(errh->ee.ee_type,  &target_errh->ee.ee_type);
                __put_user(errh->ee.ee_code, &target_errh->ee.ee_code);
                __put_user(errh->ee.ee_pad, &target_errh->ee.ee_pad);
                __put_user(errh->ee.ee_info, &target_errh->ee.ee_info);
                __put_user(errh->ee.ee_data, &target_errh->ee.ee_data);
                host_to_target_sockaddr((unsigned long) &target_errh->offender,
                    (void *) &errh->offender, sizeof(errh->offender));
                break;
            }
            default:
                goto unimplemented;
            }
            break;

        default:
        unimplemented:
            gemu_log("Unsupported ancillary data: %d/%d\n",
                                        cmsg->cmsg_level, cmsg->cmsg_type);
            memcpy(target_data, data, MIN(len, tgt_len));
            if (tgt_len > len) {
                memset(target_data + len, 0, tgt_len - len);
            }
        }

        target_cmsg->cmsg_len = tswapal(TARGET_CMSG_LEN(tgt_len));
        tgt_space = TARGET_CMSG_SPACE(tgt_len);
        if (msg_controllen < tgt_space) {
            tgt_space = msg_controllen;
        }
        msg_controllen -= tgt_space;
        space += tgt_space;
        cmsg = CMSG_NXTHDR(msgh, cmsg);
        target_cmsg = TARGET_CMSG_NXTHDR(target_msgh, target_cmsg,
                                         target_cmsg_start);
    }
    unlock_user(target_cmsg, target_cmsg_addr, space);
 the_end:
    target_msgh->msg_controllen = tswapal(space);
    return 0;
}

/* do_setsockopt() Must return target values and target errnos. */
static abi_long do_setsockopt(int sockfd, int level, int optname,
                              abi_ulong optval_addr, socklen_t optlen)
{
    abi_long ret;
    int val;
    struct ip_mreqn *ip_mreq;
    struct ip_mreq_source *ip_mreq_source;

    switch(level) {
    case SOL_TCP:
        /* TCP options all take an 'int' value.  */
        if (optlen < sizeof(uint32_t))
            return -TARGET_EINVAL;

        if (get_user_u32(val, optval_addr))
            return -TARGET_EFAULT;
        ret = get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
        break;
    case SOL_IP:
        switch(optname) {
        case IP_TOS:
        case IP_TTL:
        case IP_HDRINCL:
        case IP_ROUTER_ALERT:
        case IP_RECVOPTS:
        case IP_RETOPTS:
        case IP_PKTINFO:
        case IP_MTU_DISCOVER:
        case IP_RECVERR:
        case IP_RECVTTL:
        case IP_RECVTOS:
#ifdef IP_FREEBIND
        case IP_FREEBIND:
#endif
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
            val = 0;
            if (optlen >= sizeof(uint32_t)) {
                if (get_user_u32(val, optval_addr))
                    return -TARGET_EFAULT;
            } else if (optlen >= 1) {
                if (get_user_u8(val, optval_addr))
                    return -TARGET_EFAULT;
            }
            ret = get_errno(setsockopt(sockfd, level, optname, &val, sizeof(val)));
            break;
        case IP_ADD_MEMBERSHIP:
        case IP_DROP_MEMBERSHIP:
            if (optlen < sizeof (struct target_ip_mreq) ||
                optlen > sizeof (struct target_ip_mreqn))
                return -TARGET_EINVAL;

            ip_mreq = (struct ip_mreqn *) alloca(optlen);
            target_to_host_ip_mreq(ip_mreq, optval_addr, optlen);
            ret = get_errno(setsockopt(sockfd, level, optname, ip_mreq, optlen));
            break;

        case IP_BLOCK_SOURCE:
        case IP_UNBLOCK_SOURCE:
        case IP_ADD_SOURCE_MEMBERSHIP:
        case IP_DROP_SOURCE_MEMBERSHIP:
            if (optlen != sizeof (struct target_ip_mreq_source))
                return -TARGET_EINVAL;

            ip_mreq_source = lock_user(VERIFY_READ, optval_addr, optlen, 1);
            ret = get_errno(setsockopt(sockfd, level, optname, ip_mreq_source, optlen));
            unlock_user (ip_mreq_source, optval_addr, 0);
            break;

        default:
            goto unimplemented;
        }
        break;
    case SOL_IPV6:
        switch (optname) {
        case IPV6_MTU_DISCOVER:
        case IPV6_MTU:
        case IPV6_V6ONLY:
        case IPV6_RECVPKTINFO:
        case IPV6_UNICAST_HOPS:
        case IPV6_MULTICAST_HOPS:
        case IPV6_MULTICAST_LOOP:
        case IPV6_RECVERR:
        case IPV6_RECVHOPLIMIT:
        case IPV6_2292HOPLIMIT:
        case IPV6_CHECKSUM:
        case IPV6_ADDRFORM:
        case IPV6_2292PKTINFO:
        case IPV6_RECVTCLASS:
        case IPV6_RECVRTHDR:
        case IPV6_2292RTHDR:
        case IPV6_RECVHOPOPTS:
        case IPV6_2292HOPOPTS:
        case IPV6_RECVDSTOPTS:
        case IPV6_2292DSTOPTS:
        case IPV6_TCLASS:
#ifdef IPV6_RECVPATHMTU
        case IPV6_RECVPATHMTU:
#endif
#ifdef IPV6_TRANSPARENT
        case IPV6_TRANSPARENT:
#endif
#ifdef IPV6_FREEBIND
        case IPV6_FREEBIND:
#endif
#ifdef IPV6_RECVORIGDSTADDR
        case IPV6_RECVORIGDSTADDR:
#endif
            val = 0;
            if (optlen < sizeof(uint32_t)) {
                return -TARGET_EINVAL;
            }
            if (get_user_u32(val, optval_addr)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &val, sizeof(val)));
            break;
        case IPV6_PKTINFO:
        {
            struct in6_pktinfo pki;

            if (optlen < sizeof(pki)) {
                return -TARGET_EINVAL;
            }

            if (copy_from_user(&pki, optval_addr, sizeof(pki))) {
                return -TARGET_EFAULT;
            }

            pki.ipi6_ifindex = tswap32(pki.ipi6_ifindex);

            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &pki, sizeof(pki)));
            break;
        }
        default:
            goto unimplemented;
        }
        break;
    case SOL_ICMPV6:
        switch (optname) {
        case ICMPV6_FILTER:
        {
            struct icmp6_filter icmp6f;

            if (optlen > sizeof(icmp6f)) {
                optlen = sizeof(icmp6f);
            }

            if (copy_from_user(&icmp6f, optval_addr, optlen)) {
                return -TARGET_EFAULT;
            }

            for (val = 0; val < 8; val++) {
                icmp6f.data[val] = tswap32(icmp6f.data[val]);
            }

            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &icmp6f, optlen));
            break;
        }
        default:
            goto unimplemented;
        }
        break;
    case SOL_RAW:
        switch (optname) {
        case ICMP_FILTER:
        case IPV6_CHECKSUM:
            /* those take an u32 value */
            if (optlen < sizeof(uint32_t)) {
                return -TARGET_EINVAL;
            }

            if (get_user_u32(val, optval_addr)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(setsockopt(sockfd, level, optname,
                                       &val, sizeof(val)));
            break;

        default:
            goto unimplemented;
        }
        break;
    case TARGET_SOL_SOCKET:
        switch (optname) {
        case TARGET_SO_RCVTIMEO:
        {
                struct timeval tv;

                optname = SO_RCVTIMEO;

set_timeout:
                if (optlen != sizeof(struct target_timeval)) {
                    return -TARGET_EINVAL;
                }

                if (copy_from_user_timeval(&tv, optval_addr)) {
                    return -TARGET_EFAULT;
                }

                ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname,
                                &tv, sizeof(tv)));
                return ret;
        }
        case TARGET_SO_SNDTIMEO:
                optname = SO_SNDTIMEO;
                goto set_timeout;
        case TARGET_SO_ATTACH_FILTER:
        {
                struct target_sock_fprog *tfprog;
                struct target_sock_filter *tfilter;
                struct sock_fprog fprog;
                struct sock_filter *filter;
                int i;

                if (optlen != sizeof(*tfprog)) {
                    return -TARGET_EINVAL;
                }
                if (!lock_user_struct(VERIFY_READ, tfprog, optval_addr, 0)) {
                    return -TARGET_EFAULT;
                }
                if (!lock_user_struct(VERIFY_READ, tfilter,
                                      tswapal(tfprog->filter), 0)) {
                    unlock_user_struct(tfprog, optval_addr, 1);
                    return -TARGET_EFAULT;
                }

                fprog.len = tswap16(tfprog->len);
                filter = g_try_new(struct sock_filter, fprog.len);
                if (filter == NULL) {
                    unlock_user_struct(tfilter, tfprog->filter, 1);
                    unlock_user_struct(tfprog, optval_addr, 1);
                    return -TARGET_ENOMEM;
                }
                for (i = 0; i < fprog.len; i++) {
                    filter[i].code = tswap16(tfilter[i].code);
                    filter[i].jt = tfilter[i].jt;
                    filter[i].jf = tfilter[i].jf;
                    filter[i].k = tswap32(tfilter[i].k);
                }
                fprog.filter = filter;

                ret = get_errno(setsockopt(sockfd, SOL_SOCKET,
                                SO_ATTACH_FILTER, &fprog, sizeof(fprog)));
                g_free(filter);

                unlock_user_struct(tfilter, tfprog->filter, 1);
                unlock_user_struct(tfprog, optval_addr, 1);
                return ret;
        }
	case TARGET_SO_BINDTODEVICE:
	{
		char *dev_ifname, *addr_ifname;

		if (optlen > IFNAMSIZ - 1) {
		    optlen = IFNAMSIZ - 1;
		}
		dev_ifname = lock_user(VERIFY_READ, optval_addr, optlen, 1);
		if (!dev_ifname) {
		    return -TARGET_EFAULT;
		}
		optname = SO_BINDTODEVICE;
		addr_ifname = alloca(IFNAMSIZ);
		memcpy(addr_ifname, dev_ifname, optlen);
		addr_ifname[optlen] = 0;
		ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname,
                                           addr_ifname, optlen));
		unlock_user (dev_ifname, optval_addr, 0);
		return ret;
	}
        case TARGET_SO_LINGER:
        {
                struct linger lg;
                struct target_linger *tlg;

                if (optlen != sizeof(struct target_linger)) {
                    return -TARGET_EINVAL;
                }
                if (!lock_user_struct(VERIFY_READ, tlg, optval_addr, 1)) {
                    return -TARGET_EFAULT;
                }
                __get_user(lg.l_onoff, &tlg->l_onoff);
                __get_user(lg.l_linger, &tlg->l_linger);
                ret = get_errno(setsockopt(sockfd, SOL_SOCKET, SO_LINGER,
                                &lg, sizeof(lg)));
                unlock_user_struct(tlg, optval_addr, 0);
                return ret;
        }
            /* Options with 'int' argument.  */
        case TARGET_SO_DEBUG:
		optname = SO_DEBUG;
		break;
        case TARGET_SO_REUSEADDR:
		optname = SO_REUSEADDR;
		break;
#ifdef SO_REUSEPORT
        case TARGET_SO_REUSEPORT:
                optname = SO_REUSEPORT;
                break;
#endif
        case TARGET_SO_TYPE:
		optname = SO_TYPE;
		break;
        case TARGET_SO_ERROR:
		optname = SO_ERROR;
		break;
        case TARGET_SO_DONTROUTE:
		optname = SO_DONTROUTE;
		break;
        case TARGET_SO_BROADCAST:
		optname = SO_BROADCAST;
		break;
        case TARGET_SO_SNDBUF:
		optname = SO_SNDBUF;
		break;
        case TARGET_SO_SNDBUFFORCE:
                optname = SO_SNDBUFFORCE;
                break;
        case TARGET_SO_RCVBUF:
		optname = SO_RCVBUF;
		break;
        case TARGET_SO_RCVBUFFORCE:
                optname = SO_RCVBUFFORCE;
                break;
        case TARGET_SO_KEEPALIVE:
		optname = SO_KEEPALIVE;
		break;
        case TARGET_SO_OOBINLINE:
		optname = SO_OOBINLINE;
		break;
        case TARGET_SO_NO_CHECK:
		optname = SO_NO_CHECK;
		break;
        case TARGET_SO_PRIORITY:
		optname = SO_PRIORITY;
		break;
#ifdef SO_BSDCOMPAT
        case TARGET_SO_BSDCOMPAT:
		optname = SO_BSDCOMPAT;
		break;
#endif
        case TARGET_SO_PASSCRED:
		optname = SO_PASSCRED;
		break;
        case TARGET_SO_PASSSEC:
                optname = SO_PASSSEC;
                break;
        case TARGET_SO_TIMESTAMP:
		optname = SO_TIMESTAMP;
		break;
        case TARGET_SO_RCVLOWAT:
		optname = SO_RCVLOWAT;
		break;
        default:
            goto unimplemented;
        }
	if (optlen < sizeof(uint32_t))
            return -TARGET_EINVAL;

	if (get_user_u32(val, optval_addr))
            return -TARGET_EFAULT;
	ret = get_errno(setsockopt(sockfd, SOL_SOCKET, optname, &val, sizeof(val)));
        break;
    default:
    unimplemented:
        gemu_log("Unsupported setsockopt level=%d optname=%d\n", level, optname);
        ret = -TARGET_ENOPROTOOPT;
    }
    return ret;
}

/* do_getsockopt() Must return target values and target errnos. */
static abi_long do_getsockopt(int sockfd, int level, int optname,
                              abi_ulong optval_addr, abi_ulong optlen)
{
    abi_long ret;
    int len, val;
    socklen_t lv;

    switch(level) {
    case TARGET_SOL_SOCKET:
        level = SOL_SOCKET;
        switch (optname) {
        /* These don't just return a single integer */
        case TARGET_SO_RCVTIMEO:
        case TARGET_SO_SNDTIMEO:
        case TARGET_SO_PEERNAME:
            goto unimplemented;
        case TARGET_SO_PEERCRED: {
            struct ucred cr;
            socklen_t crlen;
            struct target_ucred *tcr;

            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }

            crlen = sizeof(cr);
            ret = get_errno(getsockopt(sockfd, level, SO_PEERCRED,
                                       &cr, &crlen));
            if (ret < 0) {
                return ret;
            }
            if (len > crlen) {
                len = crlen;
            }
            if (!lock_user_struct(VERIFY_WRITE, tcr, optval_addr, 0)) {
                return -TARGET_EFAULT;
            }
            __put_user(cr.pid, &tcr->pid);
            __put_user(cr.uid, &tcr->uid);
            __put_user(cr.gid, &tcr->gid);
            unlock_user_struct(tcr, optval_addr, 1);
            if (put_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            break;
        }
        case TARGET_SO_LINGER:
        {
            struct linger lg;
            socklen_t lglen;
            struct target_linger *tlg;

            if (get_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            if (len < 0) {
                return -TARGET_EINVAL;
            }

            lglen = sizeof(lg);
            ret = get_errno(getsockopt(sockfd, level, SO_LINGER,
                                       &lg, &lglen));
            if (ret < 0) {
                return ret;
            }
            if (len > lglen) {
                len = lglen;
            }
            if (!lock_user_struct(VERIFY_WRITE, tlg, optval_addr, 0)) {
                return -TARGET_EFAULT;
            }
            __put_user(lg.l_onoff, &tlg->l_onoff);
            __put_user(lg.l_linger, &tlg->l_linger);
            unlock_user_struct(tlg, optval_addr, 1);
            if (put_user_u32(len, optlen)) {
                return -TARGET_EFAULT;
            }
            break;
        }
        /* Options with 'int' argument.  */
        case TARGET_SO_DEBUG:
            optname = SO_DEBUG;
            goto int_case;
        case TARGET_SO_REUSEADDR:
            optname = SO_REUSEADDR;
            goto int_case;
#ifdef SO_REUSEPORT
        case TARGET_SO_REUSEPORT:
            optname = SO_REUSEPORT;
            goto int_case;
#endif
        case TARGET_SO_TYPE:
            optname = SO_TYPE;
            goto int_case;
        case TARGET_SO_ERROR:
            optname = SO_ERROR;
            goto int_case;
        case TARGET_SO_DONTROUTE:
            optname = SO_DONTROUTE;
            goto int_case;
        case TARGET_SO_BROADCAST:
            optname = SO_BROADCAST;
            goto int_case;
        case TARGET_SO_SNDBUF:
            optname = SO_SNDBUF;
            goto int_case;
        case TARGET_SO_RCVBUF:
            optname = SO_RCVBUF;
            goto int_case;
        case TARGET_SO_KEEPALIVE:
            optname = SO_KEEPALIVE;
            goto int_case;
        case TARGET_SO_OOBINLINE:
            optname = SO_OOBINLINE;
            goto int_case;
        case TARGET_SO_NO_CHECK:
            optname = SO_NO_CHECK;
            goto int_case;
        case TARGET_SO_PRIORITY:
            optname = SO_PRIORITY;
            goto int_case;
#ifdef SO_BSDCOMPAT
        case TARGET_SO_BSDCOMPAT:
            optname = SO_BSDCOMPAT;
            goto int_case;
#endif
        case TARGET_SO_PASSCRED:
            optname = SO_PASSCRED;
            goto int_case;
        case TARGET_SO_TIMESTAMP:
            optname = SO_TIMESTAMP;
            goto int_case;
        case TARGET_SO_RCVLOWAT:
            optname = SO_RCVLOWAT;
            goto int_case;
        case TARGET_SO_ACCEPTCONN:
            optname = SO_ACCEPTCONN;
            goto int_case;
        default:
            goto int_case;
        }
        break;
    case SOL_TCP:
        /* TCP options all take an 'int' value.  */
    int_case:
        if (get_user_u32(len, optlen))
            return -TARGET_EFAULT;
        if (len < 0)
            return -TARGET_EINVAL;
        lv = sizeof(lv);
        ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
        if (ret < 0)
            return ret;
        if (optname == SO_TYPE) {
            val = host_to_target_sock_type(val);
        }
        if (len > lv)
            len = lv;
        if (len == 4) {
            if (put_user_u32(val, optval_addr))
                return -TARGET_EFAULT;
        } else {
            if (put_user_u8(val, optval_addr))
                return -TARGET_EFAULT;
        }
        if (put_user_u32(len, optlen))
            return -TARGET_EFAULT;
        break;
    case SOL_IP:
        switch(optname) {
        case IP_TOS:
        case IP_TTL:
        case IP_HDRINCL:
        case IP_ROUTER_ALERT:
        case IP_RECVOPTS:
        case IP_RETOPTS:
        case IP_PKTINFO:
        case IP_MTU_DISCOVER:
        case IP_RECVERR:
        case IP_RECVTOS:
#ifdef IP_FREEBIND
        case IP_FREEBIND:
#endif
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
            if (get_user_u32(len, optlen))
                return -TARGET_EFAULT;
            if (len < 0)
                return -TARGET_EINVAL;
            lv = sizeof(lv);
            ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
            if (ret < 0)
                return ret;
            if (len < sizeof(int) && len > 0 && val >= 0 && val < 255) {
                len = 1;
                if (put_user_u32(len, optlen)
                    || put_user_u8(val, optval_addr))
                    return -TARGET_EFAULT;
            } else {
                if (len > sizeof(int))
                    len = sizeof(int);
                if (put_user_u32(len, optlen)
                    || put_user_u32(val, optval_addr))
                    return -TARGET_EFAULT;
            }
            break;
        default:
            ret = -TARGET_ENOPROTOOPT;
            break;
        }
        break;
    case SOL_IPV6:
        switch (optname) {
        case IPV6_MTU_DISCOVER:
        case IPV6_MTU:
        case IPV6_V6ONLY:
        case IPV6_RECVPKTINFO:
        case IPV6_UNICAST_HOPS:
        case IPV6_MULTICAST_HOPS:
        case IPV6_MULTICAST_LOOP:
        case IPV6_RECVERR:
        case IPV6_RECVHOPLIMIT:
        case IPV6_2292HOPLIMIT:
        case IPV6_CHECKSUM:
        case IPV6_ADDRFORM:
        case IPV6_2292PKTINFO:
        case IPV6_RECVTCLASS:
        case IPV6_RECVRTHDR:
        case IPV6_2292RTHDR:
        case IPV6_RECVHOPOPTS:
        case IPV6_2292HOPOPTS:
        case IPV6_RECVDSTOPTS:
        case IPV6_2292DSTOPTS:
        case IPV6_TCLASS:
#ifdef IPV6_RECVPATHMTU
        case IPV6_RECVPATHMTU:
#endif
#ifdef IPV6_TRANSPARENT
        case IPV6_TRANSPARENT:
#endif
#ifdef IPV6_FREEBIND
        case IPV6_FREEBIND:
#endif
#ifdef IPV6_RECVORIGDSTADDR
        case IPV6_RECVORIGDSTADDR:
#endif
            if (get_user_u32(len, optlen))
                return -TARGET_EFAULT;
            if (len < 0)
                return -TARGET_EINVAL;
            lv = sizeof(lv);
            ret = get_errno(getsockopt(sockfd, level, optname, &val, &lv));
            if (ret < 0)
                return ret;
            if (len < sizeof(int) && len > 0 && val >= 0 && val < 255) {
                len = 1;
                if (put_user_u32(len, optlen)
                    || put_user_u8(val, optval_addr))
                    return -TARGET_EFAULT;
            } else {
                if (len > sizeof(int))
                    len = sizeof(int);
                if (put_user_u32(len, optlen)
                    || put_user_u32(val, optval_addr))
                    return -TARGET_EFAULT;
            }
            break;
        default:
            ret = -TARGET_ENOPROTOOPT;
            break;
        }
        break;
    default:
    unimplemented:
        gemu_log("getsockopt level=%d optname=%d not yet supported\n",
                 level, optname);
        ret = -TARGET_EOPNOTSUPP;
        break;
    }
    return ret;
}

static struct iovec *lock_iovec(int type, abi_ulong target_addr,
                                abi_ulong count, int copy)
{
    struct target_iovec *target_vec;
    struct iovec *vec;
    abi_ulong total_len, max_len;
    int i;
    int err = 0;
    bool bad_address = false;

    if (count == 0) {
        errno = 0;
        return NULL;
    }
    if (count > IOV_MAX) {
        errno = EINVAL;
        return NULL;
    }

    vec = g_try_new0(struct iovec, count);
    if (vec == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec == NULL) {
        err = EFAULT;
        goto fail2;
    }

    /* ??? If host page size > target page size, this will result in a
       value larger than what we can actually support.  */
    max_len = 0x7fffffff & TARGET_PAGE_MASK;
    total_len = 0;

    for (i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);
        abi_long len = tswapal(target_vec[i].iov_len);

        if (len < 0) {
            err = EINVAL;
            goto fail;
        } else if (len == 0) {
            /* Zero length pointer is ignored.  */
            vec[i].iov_base = 0;
        } else {
            vec[i].iov_base = lock_user(type, base, len, copy);
            /* If the first buffer pointer is bad, this is a fault.  But
             * subsequent bad buffers will result in a partial write; this
             * is realized by filling the vector with null pointers and
             * zero lengths. */
            if (!vec[i].iov_base) {
                if (i == 0) {
                    err = EFAULT;
                    goto fail;
                } else {
                    bad_address = true;
                }
            }
            if (bad_address) {
                len = 0;
            }
            if (len > max_len - total_len) {
                len = max_len - total_len;
            }
        }
        vec[i].iov_len = len;
        total_len += len;
    }

    unlock_user(target_vec, target_addr, 0);
    return vec;

 fail:
    while (--i >= 0) {
        if (tswapal(target_vec[i].iov_len) > 0) {
            unlock_user(vec[i].iov_base, tswapal(target_vec[i].iov_base), 0);
        }
    }
    unlock_user(target_vec, target_addr, 0);
 fail2:
    g_free(vec);
    errno = err;
    return NULL;
}

static void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
                         abi_ulong count, int copy)
{
    struct target_iovec *target_vec;
    int i;

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec) {
        for (i = 0; i < count; i++) {
            abi_ulong base = tswapal(target_vec[i].iov_base);
            abi_long len = tswapal(target_vec[i].iov_len);
            if (len < 0) {
                break;
            }
            unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
        }
        unlock_user(target_vec, target_addr, 0);
    }

    g_free(vec);
}

static inline int target_to_host_sock_type(int *type)
{
    int host_type = 0;
    int target_type = *type;

    switch (target_type & TARGET_SOCK_TYPE_MASK) {
    case TARGET_SOCK_DGRAM:
        host_type = SOCK_DGRAM;
        break;
    case TARGET_SOCK_STREAM:
        host_type = SOCK_STREAM;
        break;
    default:
        host_type = target_type & TARGET_SOCK_TYPE_MASK;
        break;
    }
    if (target_type & TARGET_SOCK_CLOEXEC) {
#if defined(SOCK_CLOEXEC)
        host_type |= SOCK_CLOEXEC;
#else
        return -TARGET_EINVAL;
#endif
    }
    if (target_type & TARGET_SOCK_NONBLOCK) {
#if defined(SOCK_NONBLOCK)
        host_type |= SOCK_NONBLOCK;
#elif !defined(O_NONBLOCK)
        return -TARGET_EINVAL;
#endif
    }
    *type = host_type;
    return 0;
}

/* Try to emulate socket type flags after socket creation.  */
static int sock_flags_fixup(int fd, int target_type)
{
#if !defined(SOCK_NONBLOCK) && defined(O_NONBLOCK)
    if (target_type & TARGET_SOCK_NONBLOCK) {
        int flags = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, O_NONBLOCK | flags) == -1) {
            close(fd);
            return -TARGET_EINVAL;
        }
    }
#endif
    return fd;
}

/* do_socket() Must return target values and target errnos. */
static abi_long do_socket(int domain, int type, int protocol)
{
    int target_type = type;
    int ret;

    ret = target_to_host_sock_type(&type);
    if (ret) {
        return ret;
    }

    if (domain == PF_NETLINK && !(
#ifdef CONFIG_RTNETLINK
         protocol == NETLINK_ROUTE ||
#endif
         protocol == NETLINK_KOBJECT_UEVENT ||
         protocol == NETLINK_AUDIT)) {
        return -EPFNOSUPPORT;
    }

    if (domain == AF_PACKET ||
        (domain == AF_INET && type == SOCK_PACKET)) {
        protocol = tswap16(protocol);
    }

    ret = get_errno(socket(domain, type, protocol));
    if (ret >= 0) {
        ret = sock_flags_fixup(ret, target_type);
        if (type == SOCK_PACKET) {
            /* Manage an obsolete case :
             * if socket type is SOCK_PACKET, bind by name
             */
            fd_trans_register(ret, &target_packet_trans);
        } else if (domain == PF_NETLINK) {
            switch (protocol) {
#ifdef CONFIG_RTNETLINK
            case NETLINK_ROUTE:
                fd_trans_register(ret, &target_netlink_route_trans);
                break;
#endif
            case NETLINK_KOBJECT_UEVENT:
                /* nothing to do: messages are strings */
                break;
            case NETLINK_AUDIT:
                fd_trans_register(ret, &target_netlink_audit_trans);
                break;
            default:
                g_assert_not_reached();
            }
        }
    }
    return ret;
}

/* do_bind() Must return target values and target errnos. */
static abi_long do_bind(int sockfd, abi_ulong target_addr,
                        socklen_t addrlen)
{
    void *addr;
    abi_long ret;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    addr = alloca(addrlen+1);

    ret = target_to_host_sockaddr(sockfd, addr, target_addr, addrlen);
    if (ret)
        return ret;

    return get_errno(bind(sockfd, addr, addrlen));
}

/* do_connect() Must return target values and target errnos. */
static abi_long do_connect(int sockfd, abi_ulong target_addr,
                           socklen_t addrlen)
{
    void *addr;
    abi_long ret;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    addr = alloca(addrlen+1);

    ret = target_to_host_sockaddr(sockfd, addr, target_addr, addrlen);
    if (ret)
        return ret;

    return get_errno(safe_connect(sockfd, addr, addrlen));
}

/* do_sendrecvmsg_locked() Must return target values and target errnos. */
static abi_long do_sendrecvmsg_locked(int fd, struct target_msghdr *msgp,
                                      int flags, int send)
{
    abi_long ret, len;
    struct msghdr msg;
    abi_ulong count;
    struct iovec *vec;
    abi_ulong target_vec;

    if (msgp->msg_name) {
        msg.msg_namelen = tswap32(msgp->msg_namelen);
        msg.msg_name = alloca(msg.msg_namelen+1);
        ret = target_to_host_sockaddr(fd, msg.msg_name,
                                      tswapal(msgp->msg_name),
                                      msg.msg_namelen);
        if (ret == -TARGET_EFAULT) {
            /* For connected sockets msg_name and msg_namelen must
             * be ignored, so returning EFAULT immediately is wrong.
             * Instead, pass a bad msg_name to the host kernel, and
             * let it decide whether to return EFAULT or not.
             */
            msg.msg_name = (void *)-1;
        } else if (ret) {
            goto out2;
        }
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    msg.msg_controllen = 2 * tswapal(msgp->msg_controllen);
    msg.msg_control = alloca(msg.msg_controllen);
    memset(msg.msg_control, 0, msg.msg_controllen);

    msg.msg_flags = tswap32(msgp->msg_flags);

    count = tswapal(msgp->msg_iovlen);
    target_vec = tswapal(msgp->msg_iov);

    if (count > IOV_MAX) {
        /* sendrcvmsg returns a different errno for this condition than
         * readv/writev, so we must catch it here before lock_iovec() does.
         */
        ret = -TARGET_EMSGSIZE;
        goto out2;
    }

    vec = lock_iovec(send ? VERIFY_READ : VERIFY_WRITE,
                     target_vec, count, send);
    if (vec == NULL) {
        ret = -host_to_target_errno(errno);
        goto out2;
    }
    msg.msg_iovlen = count;
    msg.msg_iov = vec;

    if (send) {
        if (fd_trans_target_to_host_data(fd)) {
            void *host_msg;

            host_msg = g_malloc(msg.msg_iov->iov_len);
            memcpy(host_msg, msg.msg_iov->iov_base, msg.msg_iov->iov_len);
            ret = fd_trans_target_to_host_data(fd)(host_msg,
                                                   msg.msg_iov->iov_len);
            if (ret >= 0) {
                msg.msg_iov->iov_base = host_msg;
                ret = get_errno(safe_sendmsg(fd, &msg, flags));
            }
            g_free(host_msg);
        } else {
            ret = target_to_host_cmsg(&msg, msgp);
            if (ret == 0) {
                ret = get_errno(safe_sendmsg(fd, &msg, flags));
            }
        }
    } else {
        ret = get_errno(safe_recvmsg(fd, &msg, flags));
        if (!is_error(ret)) {
            len = ret;
            if (fd_trans_host_to_target_data(fd)) {
                ret = fd_trans_host_to_target_data(fd)(msg.msg_iov->iov_base,
                                               MIN(msg.msg_iov->iov_len, len));
            } else {
                ret = host_to_target_cmsg(msgp, &msg);
            }
            if (!is_error(ret)) {
                msgp->msg_namelen = tswap32(msg.msg_namelen);
                msgp->msg_flags = tswap32(msg.msg_flags);
                if (msg.msg_name != NULL && msg.msg_name != (void *)-1) {
                    ret = host_to_target_sockaddr(tswapal(msgp->msg_name),
                                    msg.msg_name, msg.msg_namelen);
                    if (ret) {
                        goto out;
                    }
                }

                ret = len;
            }
        }
    }

out:
    unlock_iovec(vec, target_vec, count, !send);
out2:
    return ret;
}

static abi_long do_sendrecvmsg(int fd, abi_ulong target_msg,
                               int flags, int send)
{
    abi_long ret;
    struct target_msghdr *msgp;

    if (!lock_user_struct(send ? VERIFY_READ : VERIFY_WRITE,
                          msgp,
                          target_msg,
                          send ? 1 : 0)) {
        return -TARGET_EFAULT;
    }
    ret = do_sendrecvmsg_locked(fd, msgp, flags, send);
    unlock_user_struct(msgp, target_msg, send ? 0 : 1);
    return ret;
}

/* We don't rely on the C library to have sendmmsg/recvmmsg support,
 * so it might not have this *mmsg-specific flag either.
 */
#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE 0x10000
#endif

static abi_long do_sendrecvmmsg(int fd, abi_ulong target_msgvec,
                                unsigned int vlen, unsigned int flags,
                                int send)
{
    struct target_mmsghdr *mmsgp;
    abi_long ret = 0;
    int i;

    if (vlen > UIO_MAXIOV) {
        vlen = UIO_MAXIOV;
    }

    mmsgp = lock_user(VERIFY_WRITE, target_msgvec, sizeof(*mmsgp) * vlen, 1);
    if (!mmsgp) {
        return -TARGET_EFAULT;
    }

    for (i = 0; i < vlen; i++) {
        ret = do_sendrecvmsg_locked(fd, &mmsgp[i].msg_hdr, flags, send);
        if (is_error(ret)) {
            break;
        }
        mmsgp[i].msg_len = tswap32(ret);
        /* MSG_WAITFORONE turns on MSG_DONTWAIT after one packet */
        if (flags & MSG_WAITFORONE) {
            flags |= MSG_DONTWAIT;
        }
    }

    unlock_user(mmsgp, target_msgvec, sizeof(*mmsgp) * i);

    /* Return number of datagrams sent if we sent any at all;
     * otherwise return the error.
     */
    if (i) {
        return i;
    }
    return ret;
}

/* do_accept4() Must return target values and target errnos. */
static abi_long do_accept4(int fd, abi_ulong target_addr,
                           abi_ulong target_addrlen_addr, int flags)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    abi_long ret;
    int host_flags;

    host_flags = target_to_host_bitmask(flags, fcntl_flags_tbl);

    if (target_addr == 0) {
        return get_errno(safe_accept4(fd, NULL, NULL, host_flags));
    }

    /* linux returns EINVAL if addrlen pointer is invalid */
    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EINVAL;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, target_addr, addrlen))
        return -TARGET_EINVAL;

    addr = alloca(addrlen);

    ret_addrlen = addrlen;
    ret = get_errno(safe_accept4(fd, addr, &ret_addrlen, host_flags));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, MIN(addrlen, ret_addrlen));
        if (put_user_u32(ret_addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

/* do_getpeername() Must return target values and target errnos. */
static abi_long do_getpeername(int fd, abi_ulong target_addr,
                               abi_ulong target_addrlen_addr)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, target_addr, addrlen))
        return -TARGET_EFAULT;

    addr = alloca(addrlen);

    ret_addrlen = addrlen;
    ret = get_errno(getpeername(fd, addr, &ret_addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, MIN(addrlen, ret_addrlen));
        if (put_user_u32(ret_addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

/* do_getsockname() Must return target values and target errnos. */
static abi_long do_getsockname(int fd, abi_ulong target_addr,
                               abi_ulong target_addrlen_addr)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr))
        return -TARGET_EFAULT;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, target_addr, addrlen))
        return -TARGET_EFAULT;

    addr = alloca(addrlen);

    ret_addrlen = addrlen;
    ret = get_errno(getsockname(fd, addr, &ret_addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, MIN(addrlen, ret_addrlen));
        if (put_user_u32(ret_addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

/* do_socketpair() Must return target values and target errnos. */
static abi_long do_socketpair(int domain, int type, int protocol,
                              abi_ulong target_tab_addr)
{
    int tab[2];
    abi_long ret;

    target_to_host_sock_type(&type);

    ret = get_errno(socketpair(domain, type, protocol, tab));
    if (!is_error(ret)) {
        if (put_user_s32(tab[0], target_tab_addr)
            || put_user_s32(tab[1], target_tab_addr + sizeof(tab[0])))
            ret = -TARGET_EFAULT;
    }
    return ret;
}

/* do_sendto() Must return target values and target errnos. */
static abi_long do_sendto(int fd, abi_ulong msg, size_t len, int flags,
                          abi_ulong target_addr, socklen_t addrlen)
{
    void *addr;
    void *host_msg;
    void *copy_msg = NULL;
    abi_long ret;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    host_msg = lock_user(VERIFY_READ, msg, len, 1);
    if (!host_msg)
        return -TARGET_EFAULT;
    if (fd_trans_target_to_host_data(fd)) {
        copy_msg = host_msg;
        host_msg = g_malloc(len);
        memcpy(host_msg, copy_msg, len);
        ret = fd_trans_target_to_host_data(fd)(host_msg, len);
        if (ret < 0) {
            goto fail;
        }
    }
    if (target_addr) {
        addr = alloca(addrlen+1);
        ret = target_to_host_sockaddr(fd, addr, target_addr, addrlen);
        if (ret) {
            goto fail;
        }
        ret = get_errno(safe_sendto(fd, host_msg, len, flags, addr, addrlen));
    } else {
        ret = get_errno(safe_sendto(fd, host_msg, len, flags, NULL, 0));
    }
fail:
    if (copy_msg) {
        g_free(host_msg);
        host_msg = copy_msg;
    }
    unlock_user(host_msg, msg, 0);
    return ret;
}

/* do_recvfrom() Must return target values and target errnos. */
static abi_long do_recvfrom(int fd, abi_ulong msg, size_t len, int flags,
                            abi_ulong target_addr,
                            abi_ulong target_addrlen)
{
    socklen_t addrlen, ret_addrlen;
    void *addr;
    void *host_msg;
    abi_long ret;

    host_msg = lock_user(VERIFY_WRITE, msg, len, 0);
    if (!host_msg)
        return -TARGET_EFAULT;
    if (target_addr) {
        if (get_user_u32(addrlen, target_addrlen)) {
            ret = -TARGET_EFAULT;
            goto fail;
        }
        if ((int)addrlen < 0) {
            ret = -TARGET_EINVAL;
            goto fail;
        }
        addr = alloca(addrlen);
        ret_addrlen = addrlen;
        ret = get_errno(safe_recvfrom(fd, host_msg, len, flags,
                                      addr, &ret_addrlen));
    } else {
        addr = NULL; /* To keep compiler quiet.  */
        addrlen = 0; /* To keep compiler quiet.  */
        ret = get_errno(safe_recvfrom(fd, host_msg, len, flags, NULL, 0));
    }
    if (!is_error(ret)) {
        if (fd_trans_host_to_target_data(fd)) {
            abi_long trans;
            trans = fd_trans_host_to_target_data(fd)(host_msg, MIN(ret, len));
            if (is_error(trans)) {
                ret = trans;
                goto fail;
            }
        }
        if (target_addr) {
            host_to_target_sockaddr(target_addr, addr,
                                    MIN(addrlen, ret_addrlen));
            if (put_user_u32(ret_addrlen, target_addrlen)) {
                ret = -TARGET_EFAULT;
                goto fail;
            }
        }
        unlock_user(host_msg, msg, len);
    } else {
fail:
        unlock_user(host_msg, msg, 0);
    }
    return ret;
}

#ifdef TARGET_NR_socketcall
/* do_socketcall() must return target values and target errnos. */
static abi_long do_socketcall(int num, abi_ulong vptr)
{
    static const unsigned nargs[] = { /* number of arguments per operation */
        [TARGET_SYS_SOCKET] = 3,      /* domain, type, protocol */
        [TARGET_SYS_BIND] = 3,        /* fd, addr, addrlen */
        [TARGET_SYS_CONNECT] = 3,     /* fd, addr, addrlen */
        [TARGET_SYS_LISTEN] = 2,      /* fd, backlog */
        [TARGET_SYS_ACCEPT] = 3,      /* fd, addr, addrlen */
        [TARGET_SYS_GETSOCKNAME] = 3, /* fd, addr, addrlen */
        [TARGET_SYS_GETPEERNAME] = 3, /* fd, addr, addrlen */
        [TARGET_SYS_SOCKETPAIR] = 4,  /* domain, type, protocol, tab */
        [TARGET_SYS_SEND] = 4,        /* fd, msg, len, flags */
        [TARGET_SYS_RECV] = 4,        /* fd, msg, len, flags */
        [TARGET_SYS_SENDTO] = 6,      /* fd, msg, len, flags, addr, addrlen */
        [TARGET_SYS_RECVFROM] = 6,    /* fd, msg, len, flags, addr, addrlen */
        [TARGET_SYS_SHUTDOWN] = 2,    /* fd, how */
        [TARGET_SYS_SETSOCKOPT] = 5,  /* fd, level, optname, optval, optlen */
        [TARGET_SYS_GETSOCKOPT] = 5,  /* fd, level, optname, optval, optlen */
        [TARGET_SYS_SENDMSG] = 3,     /* fd, msg, flags */
        [TARGET_SYS_RECVMSG] = 3,     /* fd, msg, flags */
        [TARGET_SYS_ACCEPT4] = 4,     /* fd, addr, addrlen, flags */
        [TARGET_SYS_RECVMMSG] = 4,    /* fd, msgvec, vlen, flags */
        [TARGET_SYS_SENDMMSG] = 4,    /* fd, msgvec, vlen, flags */
    };
    abi_long a[6]; /* max 6 args */
    unsigned i;

    /* check the range of the first argument num */
    /* (TARGET_SYS_SENDMMSG is the highest among TARGET_SYS_xxx) */
    if (num < 1 || num > TARGET_SYS_SENDMMSG) {
        return -TARGET_EINVAL;
    }
    /* ensure we have space for args */
    if (nargs[num] > ARRAY_SIZE(a)) {
        return -TARGET_EINVAL;
    }
    /* collect the arguments in a[] according to nargs[] */
    for (i = 0; i < nargs[num]; ++i) {
        if (get_user_ual(a[i], vptr + i * sizeof(abi_long)) != 0) {
            return -TARGET_EFAULT;
        }
    }
    /* now when we have the args, invoke the appropriate underlying function */
    switch (num) {
    case TARGET_SYS_SOCKET: /* domain, type, protocol */
        return do_socket(a[0], a[1], a[2]);
    case TARGET_SYS_BIND: /* sockfd, addr, addrlen */
        return do_bind(a[0], a[1], a[2]);
    case TARGET_SYS_CONNECT: /* sockfd, addr, addrlen */
        return do_connect(a[0], a[1], a[2]);
    case TARGET_SYS_LISTEN: /* sockfd, backlog */
        return get_errno(listen(a[0], a[1]));
    case TARGET_SYS_ACCEPT: /* sockfd, addr, addrlen */
        return do_accept4(a[0], a[1], a[2], 0);
    case TARGET_SYS_GETSOCKNAME: /* sockfd, addr, addrlen */
        return do_getsockname(a[0], a[1], a[2]);
    case TARGET_SYS_GETPEERNAME: /* sockfd, addr, addrlen */
        return do_getpeername(a[0], a[1], a[2]);
    case TARGET_SYS_SOCKETPAIR: /* domain, type, protocol, tab */
        return do_socketpair(a[0], a[1], a[2], a[3]);
    case TARGET_SYS_SEND: /* sockfd, msg, len, flags */
        return do_sendto(a[0], a[1], a[2], a[3], 0, 0);
    case TARGET_SYS_RECV: /* sockfd, msg, len, flags */
        return do_recvfrom(a[0], a[1], a[2], a[3], 0, 0);
    case TARGET_SYS_SENDTO: /* sockfd, msg, len, flags, addr, addrlen */
        return do_sendto(a[0], a[1], a[2], a[3], a[4], a[5]);
    case TARGET_SYS_RECVFROM: /* sockfd, msg, len, flags, addr, addrlen */
        return do_recvfrom(a[0], a[1], a[2], a[3], a[4], a[5]);
    case TARGET_SYS_SHUTDOWN: /* sockfd, how */
        return get_errno(shutdown(a[0], a[1]));
    case TARGET_SYS_SETSOCKOPT: /* sockfd, level, optname, optval, optlen */
        return do_setsockopt(a[0], a[1], a[2], a[3], a[4]);
    case TARGET_SYS_GETSOCKOPT: /* sockfd, level, optname, optval, optlen */
        return do_getsockopt(a[0], a[1], a[2], a[3], a[4]);
    case TARGET_SYS_SENDMSG: /* sockfd, msg, flags */
        return do_sendrecvmsg(a[0], a[1], a[2], 1);
    case TARGET_SYS_RECVMSG: /* sockfd, msg, flags */
        return do_sendrecvmsg(a[0], a[1], a[2], 0);
    case TARGET_SYS_ACCEPT4: /* sockfd, addr, addrlen, flags */
        return do_accept4(a[0], a[1], a[2], a[3]);
    case TARGET_SYS_RECVMMSG: /* sockfd, msgvec, vlen, flags */
        return do_sendrecvmmsg(a[0], a[1], a[2], a[3], 0);
    case TARGET_SYS_SENDMMSG: /* sockfd, msgvec, vlen, flags */
        return do_sendrecvmmsg(a[0], a[1], a[2], a[3], 1);
    default:
        gemu_log("Unsupported socketcall: %d\n", num);
        return -TARGET_EINVAL;
    }
}
#endif

/* kernel structure types definitions */

#define STRUCT(name, ...) STRUCT_ ## name,
#define STRUCT_SPECIAL(name) STRUCT_ ## name,
enum {
#include "syscall_types.h"
STRUCT_MAX
};
#undef STRUCT
#undef STRUCT_SPECIAL

#define STRUCT(name, ...) static const argtype struct_ ## name ## _def[] = {  __VA_ARGS__, TYPE_NULL };
#define STRUCT_SPECIAL(name)
#include "syscall_types.h"
#undef STRUCT
#undef STRUCT_SPECIAL

static const bitmask_transtbl iflag_tbl[] = {
        { TARGET_IGNBRK, TARGET_IGNBRK, IGNBRK, IGNBRK },
        { TARGET_BRKINT, TARGET_BRKINT, BRKINT, BRKINT },
        { TARGET_IGNPAR, TARGET_IGNPAR, IGNPAR, IGNPAR },
        { TARGET_PARMRK, TARGET_PARMRK, PARMRK, PARMRK },
        { TARGET_INPCK, TARGET_INPCK, INPCK, INPCK },
        { TARGET_ISTRIP, TARGET_ISTRIP, ISTRIP, ISTRIP },
        { TARGET_INLCR, TARGET_INLCR, INLCR, INLCR },
        { TARGET_IGNCR, TARGET_IGNCR, IGNCR, IGNCR },
        { TARGET_ICRNL, TARGET_ICRNL, ICRNL, ICRNL },
        { TARGET_IUCLC, TARGET_IUCLC, IUCLC, IUCLC },
        { TARGET_IXON, TARGET_IXON, IXON, IXON },
        { TARGET_IXANY, TARGET_IXANY, IXANY, IXANY },
        { TARGET_IXOFF, TARGET_IXOFF, IXOFF, IXOFF },
        { TARGET_IMAXBEL, TARGET_IMAXBEL, IMAXBEL, IMAXBEL },
        { 0, 0, 0, 0 }
};

static const bitmask_transtbl oflag_tbl[] = {
	{ TARGET_OPOST, TARGET_OPOST, OPOST, OPOST },
	{ TARGET_OLCUC, TARGET_OLCUC, OLCUC, OLCUC },
	{ TARGET_ONLCR, TARGET_ONLCR, ONLCR, ONLCR },
	{ TARGET_OCRNL, TARGET_OCRNL, OCRNL, OCRNL },
	{ TARGET_ONOCR, TARGET_ONOCR, ONOCR, ONOCR },
	{ TARGET_ONLRET, TARGET_ONLRET, ONLRET, ONLRET },
	{ TARGET_OFILL, TARGET_OFILL, OFILL, OFILL },
	{ TARGET_OFDEL, TARGET_OFDEL, OFDEL, OFDEL },
	{ TARGET_NLDLY, TARGET_NL0, NLDLY, NL0 },
	{ TARGET_NLDLY, TARGET_NL1, NLDLY, NL1 },
	{ TARGET_CRDLY, TARGET_CR0, CRDLY, CR0 },
	{ TARGET_CRDLY, TARGET_CR1, CRDLY, CR1 },
	{ TARGET_CRDLY, TARGET_CR2, CRDLY, CR2 },
	{ TARGET_CRDLY, TARGET_CR3, CRDLY, CR3 },
	{ TARGET_TABDLY, TARGET_TAB0, TABDLY, TAB0 },
	{ TARGET_TABDLY, TARGET_TAB1, TABDLY, TAB1 },
	{ TARGET_TABDLY, TARGET_TAB2, TABDLY, TAB2 },
	{ TARGET_TABDLY, TARGET_TAB3, TABDLY, TAB3 },
	{ TARGET_BSDLY, TARGET_BS0, BSDLY, BS0 },
	{ TARGET_BSDLY, TARGET_BS1, BSDLY, BS1 },
	{ TARGET_VTDLY, TARGET_VT0, VTDLY, VT0 },
	{ TARGET_VTDLY, TARGET_VT1, VTDLY, VT1 },
	{ TARGET_FFDLY, TARGET_FF0, FFDLY, FF0 },
	{ TARGET_FFDLY, TARGET_FF1, FFDLY, FF1 },
	{ 0, 0, 0, 0 }
};

static const bitmask_transtbl cflag_tbl[] = {
	{ TARGET_CBAUD, TARGET_B0, CBAUD, B0 },
	{ TARGET_CBAUD, TARGET_B50, CBAUD, B50 },
	{ TARGET_CBAUD, TARGET_B75, CBAUD, B75 },
	{ TARGET_CBAUD, TARGET_B110, CBAUD, B110 },
	{ TARGET_CBAUD, TARGET_B134, CBAUD, B134 },
	{ TARGET_CBAUD, TARGET_B150, CBAUD, B150 },
	{ TARGET_CBAUD, TARGET_B200, CBAUD, B200 },
	{ TARGET_CBAUD, TARGET_B300, CBAUD, B300 },
	{ TARGET_CBAUD, TARGET_B600, CBAUD, B600 },
	{ TARGET_CBAUD, TARGET_B1200, CBAUD, B1200 },
	{ TARGET_CBAUD, TARGET_B1800, CBAUD, B1800 },
	{ TARGET_CBAUD, TARGET_B2400, CBAUD, B2400 },
	{ TARGET_CBAUD, TARGET_B4800, CBAUD, B4800 },
	{ TARGET_CBAUD, TARGET_B9600, CBAUD, B9600 },
	{ TARGET_CBAUD, TARGET_B19200, CBAUD, B19200 },
	{ TARGET_CBAUD, TARGET_B38400, CBAUD, B38400 },
	{ TARGET_CBAUD, TARGET_B57600, CBAUD, B57600 },
	{ TARGET_CBAUD, TARGET_B115200, CBAUD, B115200 },
	{ TARGET_CBAUD, TARGET_B230400, CBAUD, B230400 },
	{ TARGET_CBAUD, TARGET_B460800, CBAUD, B460800 },
	{ TARGET_CSIZE, TARGET_CS5, CSIZE, CS5 },
	{ TARGET_CSIZE, TARGET_CS6, CSIZE, CS6 },
	{ TARGET_CSIZE, TARGET_CS7, CSIZE, CS7 },
	{ TARGET_CSIZE, TARGET_CS8, CSIZE, CS8 },
	{ TARGET_CSTOPB, TARGET_CSTOPB, CSTOPB, CSTOPB },
	{ TARGET_CREAD, TARGET_CREAD, CREAD, CREAD },
	{ TARGET_PARENB, TARGET_PARENB, PARENB, PARENB },
	{ TARGET_PARODD, TARGET_PARODD, PARODD, PARODD },
	{ TARGET_HUPCL, TARGET_HUPCL, HUPCL, HUPCL },
	{ TARGET_CLOCAL, TARGET_CLOCAL, CLOCAL, CLOCAL },
	{ TARGET_CRTSCTS, TARGET_CRTSCTS, CRTSCTS, CRTSCTS },
	{ 0, 0, 0, 0 }
};

static const bitmask_transtbl lflag_tbl[] = {
	{ TARGET_ISIG, TARGET_ISIG, ISIG, ISIG },
	{ TARGET_ICANON, TARGET_ICANON, ICANON, ICANON },
	{ TARGET_XCASE, TARGET_XCASE, XCASE, XCASE },
	{ TARGET_ECHO, TARGET_ECHO, ECHO, ECHO },
	{ TARGET_ECHOE, TARGET_ECHOE, ECHOE, ECHOE },
	{ TARGET_ECHOK, TARGET_ECHOK, ECHOK, ECHOK },
	{ TARGET_ECHONL, TARGET_ECHONL, ECHONL, ECHONL },
	{ TARGET_NOFLSH, TARGET_NOFLSH, NOFLSH, NOFLSH },
	{ TARGET_TOSTOP, TARGET_TOSTOP, TOSTOP, TOSTOP },
	{ TARGET_ECHOCTL, TARGET_ECHOCTL, ECHOCTL, ECHOCTL },
	{ TARGET_ECHOPRT, TARGET_ECHOPRT, ECHOPRT, ECHOPRT },
	{ TARGET_ECHOKE, TARGET_ECHOKE, ECHOKE, ECHOKE },
	{ TARGET_FLUSHO, TARGET_FLUSHO, FLUSHO, FLUSHO },
	{ TARGET_PENDIN, TARGET_PENDIN, PENDIN, PENDIN },
	{ TARGET_IEXTEN, TARGET_IEXTEN, IEXTEN, IEXTEN },
	{ 0, 0, 0, 0 }
};

static void target_to_host_termios (void *dst, const void *src)
{
    struct host_termios *host = dst;
    const struct target_termios *target = src;

    host->c_iflag =
        target_to_host_bitmask(tswap32(target->c_iflag), iflag_tbl);
    host->c_oflag =
        target_to_host_bitmask(tswap32(target->c_oflag), oflag_tbl);
    host->c_cflag =
        target_to_host_bitmask(tswap32(target->c_cflag), cflag_tbl);
    host->c_lflag =
        target_to_host_bitmask(tswap32(target->c_lflag), lflag_tbl);
    host->c_line = target->c_line;

    memset(host->c_cc, 0, sizeof(host->c_cc));
    host->c_cc[VINTR] = target->c_cc[TARGET_VINTR];
    host->c_cc[VQUIT] = target->c_cc[TARGET_VQUIT];
    host->c_cc[VERASE] = target->c_cc[TARGET_VERASE];
    host->c_cc[VKILL] = target->c_cc[TARGET_VKILL];
    host->c_cc[VEOF] = target->c_cc[TARGET_VEOF];
    host->c_cc[VTIME] = target->c_cc[TARGET_VTIME];
    host->c_cc[VMIN] = target->c_cc[TARGET_VMIN];
    host->c_cc[VSWTC] = target->c_cc[TARGET_VSWTC];
    host->c_cc[VSTART] = target->c_cc[TARGET_VSTART];
    host->c_cc[VSTOP] = target->c_cc[TARGET_VSTOP];
    host->c_cc[VSUSP] = target->c_cc[TARGET_VSUSP];
    host->c_cc[VEOL] = target->c_cc[TARGET_VEOL];
    host->c_cc[VREPRINT] = target->c_cc[TARGET_VREPRINT];
    host->c_cc[VDISCARD] = target->c_cc[TARGET_VDISCARD];
    host->c_cc[VWERASE] = target->c_cc[TARGET_VWERASE];
    host->c_cc[VLNEXT] = target->c_cc[TARGET_VLNEXT];
    host->c_cc[VEOL2] = target->c_cc[TARGET_VEOL2];
}

static void host_to_target_termios (void *dst, const void *src)
{
    struct target_termios *target = dst;
    const struct host_termios *host = src;

    target->c_iflag =
        tswap32(host_to_target_bitmask(host->c_iflag, iflag_tbl));
    target->c_oflag =
        tswap32(host_to_target_bitmask(host->c_oflag, oflag_tbl));
    target->c_cflag =
        tswap32(host_to_target_bitmask(host->c_cflag, cflag_tbl));
    target->c_lflag =
        tswap32(host_to_target_bitmask(host->c_lflag, lflag_tbl));
    target->c_line = host->c_line;

    memset(target->c_cc, 0, sizeof(target->c_cc));
    target->c_cc[TARGET_VINTR] = host->c_cc[VINTR];
    target->c_cc[TARGET_VQUIT] = host->c_cc[VQUIT];
    target->c_cc[TARGET_VERASE] = host->c_cc[VERASE];
    target->c_cc[TARGET_VKILL] = host->c_cc[VKILL];
    target->c_cc[TARGET_VEOF] = host->c_cc[VEOF];
    target->c_cc[TARGET_VTIME] = host->c_cc[VTIME];
    target->c_cc[TARGET_VMIN] = host->c_cc[VMIN];
    target->c_cc[TARGET_VSWTC] = host->c_cc[VSWTC];
    target->c_cc[TARGET_VSTART] = host->c_cc[VSTART];
    target->c_cc[TARGET_VSTOP] = host->c_cc[VSTOP];
    target->c_cc[TARGET_VSUSP] = host->c_cc[VSUSP];
    target->c_cc[TARGET_VEOL] = host->c_cc[VEOL];
    target->c_cc[TARGET_VREPRINT] = host->c_cc[VREPRINT];
    target->c_cc[TARGET_VDISCARD] = host->c_cc[VDISCARD];
    target->c_cc[TARGET_VWERASE] = host->c_cc[VWERASE];
    target->c_cc[TARGET_VLNEXT] = host->c_cc[VLNEXT];
    target->c_cc[TARGET_VEOL2] = host->c_cc[VEOL2];
}

static const StructEntry struct_termios_def = {
    .convert = { host_to_target_termios, target_to_host_termios },
    .size = { sizeof(struct target_termios), sizeof(struct host_termios) },
    .align = { __alignof__(struct target_termios), __alignof__(struct host_termios) },
};

#if defined(TARGET_I386)

/* NOTE: there is really one LDT for all the threads */
static uint8_t *ldt_table;

static abi_long read_ldt(abi_ulong ptr, unsigned long bytecount)
{
    int size;
    void *p;

    if (!ldt_table)
        return 0;
    size = TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE;
    if (size > bytecount)
        size = bytecount;
    p = lock_user(VERIFY_WRITE, ptr, size, 0);
    if (!p)
        return -TARGET_EFAULT;
    /* ??? Should this by byteswapped?  */
    memcpy(p, ldt_table, size);
    unlock_user(p, ptr, size);
    return size;
}

/* XXX: add locking support */
static abi_long write_ldt(CPUX86State *env,
                          abi_ulong ptr, unsigned long bytecount, int oldmode)
{
    struct target_modify_ldt_ldt_s ldt_info;
    struct target_modify_ldt_ldt_s *target_ldt_info;
    int seg_32bit, contents, read_exec_only, limit_in_pages;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;

    if (bytecount != sizeof(ldt_info))
        return -TARGET_EINVAL;
    if (!lock_user_struct(VERIFY_READ, target_ldt_info, ptr, 1))
        return -TARGET_EFAULT;
    ldt_info.entry_number = tswap32(target_ldt_info->entry_number);
    ldt_info.base_addr = tswapal(target_ldt_info->base_addr);
    ldt_info.limit = tswap32(target_ldt_info->limit);
    ldt_info.flags = tswap32(target_ldt_info->flags);
    unlock_user_struct(target_ldt_info, ptr, 0);

    if (ldt_info.entry_number >= TARGET_LDT_ENTRIES)
        return -TARGET_EINVAL;
    seg_32bit = ldt_info.flags & 1;
    contents = (ldt_info.flags >> 1) & 3;
    read_exec_only = (ldt_info.flags >> 3) & 1;
    limit_in_pages = (ldt_info.flags >> 4) & 1;
    seg_not_present = (ldt_info.flags >> 5) & 1;
    useable = (ldt_info.flags >> 6) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (ldt_info.flags >> 7) & 1;
#endif
    if (contents == 3) {
        if (oldmode)
            return -TARGET_EINVAL;
        if (seg_not_present == 0)
            return -TARGET_EINVAL;
    }
    /* allocate the LDT */
    if (!ldt_table) {
        env->ldt.base = target_mmap(0,
                                    TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE,
                                    PROT_READ|PROT_WRITE,
                                    MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        if (env->ldt.base == -1)
            return -TARGET_ENOMEM;
        memset(g2h(env->ldt.base), 0,
               TARGET_LDT_ENTRIES * TARGET_LDT_ENTRY_SIZE);
        env->ldt.limit = 0xffff;
        ldt_table = g2h(env->ldt.base);
    }

    /* NOTE: same code as Linux kernel */
    /* Allow LDTs to be cleared by the user. */
    if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
        if (oldmode ||
            (contents == 0		&&
             read_exec_only == 1	&&
             seg_32bit == 0		&&
             limit_in_pages == 0	&&
             seg_not_present == 1	&&
             useable == 0 )) {
            entry_1 = 0;
            entry_2 = 0;
            goto install;
        }
    }

    entry_1 = ((ldt_info.base_addr & 0x0000ffff) << 16) |
        (ldt_info.limit & 0x0ffff);
    entry_2 = (ldt_info.base_addr & 0xff000000) |
        ((ldt_info.base_addr & 0x00ff0000) >> 16) |
        (ldt_info.limit & 0xf0000) |
        ((read_exec_only ^ 1) << 9) |
        (contents << 10) |
        ((seg_not_present ^ 1) << 15) |
        (seg_32bit << 22) |
        (limit_in_pages << 23) |
        (lm << 21) |
        0x7000;
    if (!oldmode)
        entry_2 |= (useable << 20);

    /* Install the new entry ...  */
install:
    lp = (uint32_t *)(ldt_table + (ldt_info.entry_number << 3));
    lp[0] = tswap32(entry_1);
    lp[1] = tswap32(entry_2);
    return 0;
}

/* specific and weird i386 syscalls */
static abi_long do_modify_ldt(CPUX86State *env, int func, abi_ulong ptr,
                              unsigned long bytecount)
{
    abi_long ret;

    switch (func) {
    case 0:
        ret = read_ldt(ptr, bytecount);
        break;
    case 1:
        ret = write_ldt(env, ptr, bytecount, 1);
        break;
    case 0x11:
        ret = write_ldt(env, ptr, bytecount, 0);
        break;
    default:
        ret = -TARGET_ENOSYS;
        break;
    }
    return ret;
}

#if defined(TARGET_I386) && defined(TARGET_ABI32)
abi_long do_set_thread_area(CPUX86State *env, abi_ulong ptr)
{
    uint64_t *gdt_table = g2h(env->gdt.base);
    struct target_modify_ldt_ldt_s ldt_info;
    struct target_modify_ldt_ldt_s *target_ldt_info;
    int seg_32bit, contents, read_exec_only, limit_in_pages;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;
    int i;

    lock_user_struct(VERIFY_WRITE, target_ldt_info, ptr, 1);
    if (!target_ldt_info)
        return -TARGET_EFAULT;
    ldt_info.entry_number = tswap32(target_ldt_info->entry_number);
    ldt_info.base_addr = tswapal(target_ldt_info->base_addr);
    ldt_info.limit = tswap32(target_ldt_info->limit);
    ldt_info.flags = tswap32(target_ldt_info->flags);
    if (ldt_info.entry_number == -1) {
        for (i=TARGET_GDT_ENTRY_TLS_MIN; i<=TARGET_GDT_ENTRY_TLS_MAX; i++) {
            if (gdt_table[i] == 0) {
                ldt_info.entry_number = i;
                target_ldt_info->entry_number = tswap32(i);
                break;
            }
        }
    }
    unlock_user_struct(target_ldt_info, ptr, 1);

    if (ldt_info.entry_number < TARGET_GDT_ENTRY_TLS_MIN || 
        ldt_info.entry_number > TARGET_GDT_ENTRY_TLS_MAX)
           return -TARGET_EINVAL;
    seg_32bit = ldt_info.flags & 1;
    contents = (ldt_info.flags >> 1) & 3;
    read_exec_only = (ldt_info.flags >> 3) & 1;
    limit_in_pages = (ldt_info.flags >> 4) & 1;
    seg_not_present = (ldt_info.flags >> 5) & 1;
    useable = (ldt_info.flags >> 6) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (ldt_info.flags >> 7) & 1;
#endif

    if (contents == 3) {
        if (seg_not_present == 0)
            return -TARGET_EINVAL;
    }

    /* NOTE: same code as Linux kernel */
    /* Allow LDTs to be cleared by the user. */
    if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
        if ((contents == 0             &&
             read_exec_only == 1       &&
             seg_32bit == 0            &&
             limit_in_pages == 0       &&
             seg_not_present == 1      &&
             useable == 0 )) {
            entry_1 = 0;
            entry_2 = 0;
            goto install;
        }
    }

    entry_1 = ((ldt_info.base_addr & 0x0000ffff) << 16) |
        (ldt_info.limit & 0x0ffff);
    entry_2 = (ldt_info.base_addr & 0xff000000) |
        ((ldt_info.base_addr & 0x00ff0000) >> 16) |
        (ldt_info.limit & 0xf0000) |
        ((read_exec_only ^ 1) << 9) |
        (contents << 10) |
        ((seg_not_present ^ 1) << 15) |
        (seg_32bit << 22) |
        (limit_in_pages << 23) |
        (useable << 20) |
        (lm << 21) |
        0x7000;

    /* Install the new entry ...  */
install:
    lp = (uint32_t *)(gdt_table + ldt_info.entry_number);
    lp[0] = tswap32(entry_1);
    lp[1] = tswap32(entry_2);
    return 0;
}

static abi_long do_get_thread_area(CPUX86State *env, abi_ulong ptr)
{
    struct target_modify_ldt_ldt_s *target_ldt_info;
    uint64_t *gdt_table = g2h(env->gdt.base);
    uint32_t base_addr, limit, flags;
    int seg_32bit, contents, read_exec_only, limit_in_pages, idx;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;

    lock_user_struct(VERIFY_WRITE, target_ldt_info, ptr, 1);
    if (!target_ldt_info)
        return -TARGET_EFAULT;
    idx = tswap32(target_ldt_info->entry_number);
    if (idx < TARGET_GDT_ENTRY_TLS_MIN ||
        idx > TARGET_GDT_ENTRY_TLS_MAX) {
        unlock_user_struct(target_ldt_info, ptr, 1);
        return -TARGET_EINVAL;
    }
    lp = (uint32_t *)(gdt_table + idx);
    entry_1 = tswap32(lp[0]);
    entry_2 = tswap32(lp[1]);
    
    read_exec_only = ((entry_2 >> 9) & 1) ^ 1;
    contents = (entry_2 >> 10) & 3;
    seg_not_present = ((entry_2 >> 15) & 1) ^ 1;
    seg_32bit = (entry_2 >> 22) & 1;
    limit_in_pages = (entry_2 >> 23) & 1;
    useable = (entry_2 >> 20) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (entry_2 >> 21) & 1;
#endif
    flags = (seg_32bit << 0) | (contents << 1) |
        (read_exec_only << 3) | (limit_in_pages << 4) |
        (seg_not_present << 5) | (useable << 6) | (lm << 7);
    limit = (entry_1 & 0xffff) | (entry_2  & 0xf0000);
    base_addr = (entry_1 >> 16) | 
        (entry_2 & 0xff000000) | 
        ((entry_2 & 0xff) << 16);
    target_ldt_info->base_addr = tswapal(base_addr);
    target_ldt_info->limit = tswap32(limit);
    target_ldt_info->flags = tswap32(flags);
    unlock_user_struct(target_ldt_info, ptr, 1);
    return 0;
}
#endif /* TARGET_I386 && TARGET_ABI32 */

#ifndef TARGET_ABI32
abi_long do_arch_prctl(CPUX86State *env, int code, abi_ulong addr)
{
    abi_long ret = 0;
    abi_ulong val;
    int idx;

    switch(code) {
    case TARGET_ARCH_SET_GS:
    case TARGET_ARCH_SET_FS:
        if (code == TARGET_ARCH_SET_GS)
            idx = R_GS;
        else
            idx = R_FS;
        cpu_x86_load_seg(env, idx, 0);
        env->segs[idx].base = addr;
        break;
    case TARGET_ARCH_GET_GS:
    case TARGET_ARCH_GET_FS:
        if (code == TARGET_ARCH_GET_GS)
            idx = R_GS;
        else
            idx = R_FS;
        val = env->segs[idx].base;
        if (put_user(val, addr, abi_ulong))
            ret = -TARGET_EFAULT;
        break;
    default:
        ret = -TARGET_EINVAL;
        break;
    }
    return ret;
}
#endif

#endif /* defined(TARGET_I386) */

#define NEW_STACK_SIZE 0x40000


static pthread_mutex_t clone_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    CPUArchState *env;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    uint32_t tid;
    abi_ulong child_tidptr;
    abi_ulong parent_tidptr;
    sigset_t sigmask;
} new_thread_info;

static void *clone_func(void *arg)
{
    new_thread_info *info = arg;
    CPUArchState *env;
    CPUState *cpu;
    TaskState *ts;

    rcu_register_thread();
    tcg_register_thread();
    env = info->env;
    cpu = ENV_GET_CPU(env);
    thread_cpu = cpu;
    ts = (TaskState *)cpu->opaque;
    info->tid = sys_gettid();
    task_settid(ts);
    if (info->child_tidptr)
        put_user_u32(info->tid, info->child_tidptr);
    if (info->parent_tidptr)
        put_user_u32(info->tid, info->parent_tidptr);
    /* Enable signals.  */
    sigprocmask(SIG_SETMASK, &info->sigmask, NULL);
    /* Signal to the parent that we're ready.  */
    pthread_mutex_lock(&info->mutex);
    pthread_cond_broadcast(&info->cond);
    pthread_mutex_unlock(&info->mutex);
    /* Wait until the parent has finished initializing the tls state.  */
    pthread_mutex_lock(&clone_lock);
    pthread_mutex_unlock(&clone_lock);
    cpu_loop(env);
    /* never exits */
    return NULL;
}

#define FLOCK_TRANSTBL \
    switch (type) { \
    TRANSTBL_CONVERT(F_RDLCK); \
    TRANSTBL_CONVERT(F_WRLCK); \
    TRANSTBL_CONVERT(F_UNLCK); \
    TRANSTBL_CONVERT(F_EXLCK); \
    TRANSTBL_CONVERT(F_SHLCK); \
    }

static int target_to_host_flock(int type)
{
#define TRANSTBL_CONVERT(a) case TARGET_##a: return a
    FLOCK_TRANSTBL
#undef  TRANSTBL_CONVERT
    return -TARGET_EINVAL;
}

static int host_to_target_flock(int type)
{
#define TRANSTBL_CONVERT(a) case a: return TARGET_##a
    FLOCK_TRANSTBL
#undef  TRANSTBL_CONVERT
    /* if we don't know how to convert the value coming
     * from the host we copy to the target field as-is
     */
    return type;
}

static inline abi_long copy_from_user_flock(struct flock64 *fl,
                                            abi_ulong target_flock_addr)
{
    struct target_flock *target_fl;
    int l_type;

    if (!lock_user_struct(VERIFY_READ, target_fl, target_flock_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(l_type, &target_fl->l_type);
    l_type = target_to_host_flock(l_type);
    if (l_type < 0) {
        return l_type;
    }
    fl->l_type = l_type;
    __get_user(fl->l_whence, &target_fl->l_whence);
    __get_user(fl->l_start, &target_fl->l_start);
    __get_user(fl->l_len, &target_fl->l_len);
    __get_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 0);
    return 0;
}

static inline abi_long copy_to_user_flock(abi_ulong target_flock_addr,
                                          const struct flock64 *fl)
{
    struct target_flock *target_fl;
    short l_type;

    if (!lock_user_struct(VERIFY_WRITE, target_fl, target_flock_addr, 0)) {
        return -TARGET_EFAULT;
    }

    l_type = host_to_target_flock(fl->l_type);
    __put_user(l_type, &target_fl->l_type);
    __put_user(fl->l_whence, &target_fl->l_whence);
    __put_user(fl->l_start, &target_fl->l_start);
    __put_user(fl->l_len, &target_fl->l_len);
    __put_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 1);
    return 0;
}

typedef abi_long from_flock64_fn(struct flock64 *fl, abi_ulong target_addr);
typedef abi_long to_flock64_fn(abi_ulong target_addr, const struct flock64 *fl);

#if defined(TARGET_ARM) && TARGET_ABI_BITS == 32
static inline abi_long copy_from_user_oabi_flock64(struct flock64 *fl,
                                                   abi_ulong target_flock_addr)
{
    struct target_oabi_flock64 *target_fl;
    int l_type;

    if (!lock_user_struct(VERIFY_READ, target_fl, target_flock_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(l_type, &target_fl->l_type);
    l_type = target_to_host_flock(l_type);
    if (l_type < 0) {
        return l_type;
    }
    fl->l_type = l_type;
    __get_user(fl->l_whence, &target_fl->l_whence);
    __get_user(fl->l_start, &target_fl->l_start);
    __get_user(fl->l_len, &target_fl->l_len);
    __get_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 0);
    return 0;
}

static inline abi_long copy_to_user_oabi_flock64(abi_ulong target_flock_addr,
                                                 const struct flock64 *fl)
{
    struct target_oabi_flock64 *target_fl;
    short l_type;

    if (!lock_user_struct(VERIFY_WRITE, target_fl, target_flock_addr, 0)) {
        return -TARGET_EFAULT;
    }

    l_type = host_to_target_flock(fl->l_type);
    __put_user(l_type, &target_fl->l_type);
    __put_user(fl->l_whence, &target_fl->l_whence);
    __put_user(fl->l_start, &target_fl->l_start);
    __put_user(fl->l_len, &target_fl->l_len);
    __put_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 1);
    return 0;
}
#endif

static inline abi_long copy_from_user_flock64(struct flock64 *fl,
                                              abi_ulong target_flock_addr)
{
    struct target_flock64 *target_fl;
    int l_type;

    if (!lock_user_struct(VERIFY_READ, target_fl, target_flock_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(l_type, &target_fl->l_type);
    l_type = target_to_host_flock(l_type);
    if (l_type < 0) {
        return l_type;
    }
    fl->l_type = l_type;
    __get_user(fl->l_whence, &target_fl->l_whence);
    __get_user(fl->l_start, &target_fl->l_start);
    __get_user(fl->l_len, &target_fl->l_len);
    __get_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 0);
    return 0;
}

static inline abi_long copy_to_user_flock64(abi_ulong target_flock_addr,
                                            const struct flock64 *fl)
{
    struct target_flock64 *target_fl;
    short l_type;

    if (!lock_user_struct(VERIFY_WRITE, target_fl, target_flock_addr, 0)) {
        return -TARGET_EFAULT;
    }

    l_type = host_to_target_flock(fl->l_type);
    __put_user(l_type, &target_fl->l_type);
    __put_user(fl->l_whence, &target_fl->l_whence);
    __put_user(fl->l_start, &target_fl->l_start);
    __put_user(fl->l_len, &target_fl->l_len);
    __put_user(fl->l_pid, &target_fl->l_pid);
    unlock_user_struct(target_fl, target_flock_addr, 1);
    return 0;
}

#ifdef USE_UID16

static inline int high2lowuid(int uid)
{
    if (uid > 65535)
        return 65534;
    else
        return uid;
}

static inline int high2lowgid(int gid)
{
    if (gid > 65535)
        return 65534;
    else
        return gid;
}

static inline int low2highuid(int uid)
{
    if ((int16_t)uid == -1)
        return -1;
    else
        return uid;
}

static inline int low2highgid(int gid)
{
    if ((int16_t)gid == -1)
        return -1;
    else
        return gid;
}
static inline int tswapid(int id)
{
    return tswap16(id);
}

#define put_user_id(x, gaddr) put_user_u16(x, gaddr)

#else /* !USE_UID16 */
static inline int high2lowuid(int uid)
{
    return uid;
}
static inline int high2lowgid(int gid)
{
    return gid;
}
static inline int low2highuid(int uid)
{
    return uid;
}
static inline int low2highgid(int gid)
{
    return gid;
}
static inline int tswapid(int id)
{
    return tswap32(id);
}

#define put_user_id(x, gaddr) put_user_u32(x, gaddr)

#endif /* USE_UID16 */

/* We must do direct syscalls for setting UID/GID, because we want to
 * implement the Linux system call semantics of "change only for this thread",
 * not the libc/POSIX semantics of "change for all threads in process".
 * (See http://ewontfix.com/17/ for more details.)
 * We use the 32-bit version of the syscalls if present; if it is not
 * then either the host architecture supports 32-bit UIDs natively with
 * the standard syscall, or the 16-bit UID is the best we can do.
 */
#ifdef __NR_setuid32
#define __NR_sys_setuid __NR_setuid32
#else
#define __NR_sys_setuid __NR_setuid
#endif
#ifdef __NR_setgid32
#define __NR_sys_setgid __NR_setgid32
#else
#define __NR_sys_setgid __NR_setgid
#endif
#ifdef __NR_setresuid32
#define __NR_sys_setresuid __NR_setresuid32
#else
#define __NR_sys_setresuid __NR_setresuid
#endif
#ifdef __NR_setresgid32
#define __NR_sys_setresgid __NR_setresgid32
#else
#define __NR_sys_setresgid __NR_setresgid
#endif

_syscall1(int, sys_setuid, uid_t, uid)
_syscall1(int, sys_setgid, gid_t, gid)
_syscall3(int, sys_setresuid, uid_t, ruid, uid_t, euid, uid_t, suid)
_syscall3(int, sys_setresgid, gid_t, rgid, gid_t, egid, gid_t, sgid)

static inline uint64_t target_offset64(abi_ulong word0, abi_ulong word1)
{
#if TARGET_ABI_BITS == 64
    return word0;
#elif defined(TARGET_WORDS_BIGENDIAN)
    return ((uint64_t)word0 << 32) | word1;
#else
    return ((uint64_t)word1 << 32) | word0;
#endif
}

#ifdef TARGET_NR_truncate64
static inline abi_long target_truncate64(void *cpu_env, const char *arg1,
                                         abi_long arg2,
                                         abi_long arg3,
                                         abi_long arg4)
{
    if (regpairs_aligned(cpu_env, TARGET_NR_truncate64)) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(truncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

#ifdef TARGET_NR_ftruncate64
static inline abi_long target_ftruncate64(void *cpu_env, abi_long arg1,
                                          abi_long arg2,
                                          abi_long arg3,
                                          abi_long arg4)
{
    if (regpairs_aligned(cpu_env, TARGET_NR_ftruncate64)) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(ftruncate64(arg1, target_offset64(arg2, arg3)));
}
#endif

static inline abi_long target_to_host_timespec(struct timespec *host_ts,
                                               abi_ulong target_addr)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_READ, target_ts, target_addr, 1))
        return -TARGET_EFAULT;
    __get_user(host_ts->tv_sec, &target_ts->tv_sec);
    __get_user(host_ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_timespec(abi_ulong target_addr,
                                               struct timespec *host_ts)
{
    struct target_timespec *target_ts;

    if (!lock_user_struct(VERIFY_WRITE, target_ts, target_addr, 0))
        return -TARGET_EFAULT;
    __put_user(host_ts->tv_sec, &target_ts->tv_sec);
    __put_user(host_ts->tv_nsec, &target_ts->tv_nsec);
    unlock_user_struct(target_ts, target_addr, 1);
    return 0;
}

static inline abi_long target_to_host_itimerspec(struct itimerspec *host_itspec,
                                                 abi_ulong target_addr)
{
    struct target_itimerspec *target_itspec;

    if (!lock_user_struct(VERIFY_READ, target_itspec, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    host_itspec->it_interval.tv_sec =
                            tswapal(target_itspec->it_interval.tv_sec);
    host_itspec->it_interval.tv_nsec =
                            tswapal(target_itspec->it_interval.tv_nsec);
    host_itspec->it_value.tv_sec = tswapal(target_itspec->it_value.tv_sec);
    host_itspec->it_value.tv_nsec = tswapal(target_itspec->it_value.tv_nsec);

    unlock_user_struct(target_itspec, target_addr, 1);
    return 0;
}

static inline abi_long host_to_target_itimerspec(abi_ulong target_addr,
                                               struct itimerspec *host_its)
{
    struct target_itimerspec *target_itspec;

    if (!lock_user_struct(VERIFY_WRITE, target_itspec, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    target_itspec->it_interval.tv_sec = tswapal(host_its->it_interval.tv_sec);
    target_itspec->it_interval.tv_nsec = tswapal(host_its->it_interval.tv_nsec);

    target_itspec->it_value.tv_sec = tswapal(host_its->it_value.tv_sec);
    target_itspec->it_value.tv_nsec = tswapal(host_its->it_value.tv_nsec);

    unlock_user_struct(target_itspec, target_addr, 0);
    return 0;
}

static inline abi_long target_to_host_timex(struct timex *host_tx,
                                            abi_long target_addr)
{
    struct target_timex *target_tx;

    if (!lock_user_struct(VERIFY_READ, target_tx, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(host_tx->modes, &target_tx->modes);
    __get_user(host_tx->offset, &target_tx->offset);
    __get_user(host_tx->freq, &target_tx->freq);
    __get_user(host_tx->maxerror, &target_tx->maxerror);
    __get_user(host_tx->esterror, &target_tx->esterror);
    __get_user(host_tx->status, &target_tx->status);
    __get_user(host_tx->constant, &target_tx->constant);
    __get_user(host_tx->precision, &target_tx->precision);
    __get_user(host_tx->tolerance, &target_tx->tolerance);
    __get_user(host_tx->time.tv_sec, &target_tx->time.tv_sec);
    __get_user(host_tx->time.tv_usec, &target_tx->time.tv_usec);
    __get_user(host_tx->tick, &target_tx->tick);
    __get_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __get_user(host_tx->jitter, &target_tx->jitter);
    __get_user(host_tx->shift, &target_tx->shift);
    __get_user(host_tx->stabil, &target_tx->stabil);
    __get_user(host_tx->jitcnt, &target_tx->jitcnt);
    __get_user(host_tx->calcnt, &target_tx->calcnt);
    __get_user(host_tx->errcnt, &target_tx->errcnt);
    __get_user(host_tx->stbcnt, &target_tx->stbcnt);
    __get_user(host_tx->tai, &target_tx->tai);

    unlock_user_struct(target_tx, target_addr, 0);
    return 0;
}

static inline abi_long host_to_target_timex(abi_long target_addr,
                                            struct timex *host_tx)
{
    struct target_timex *target_tx;

    if (!lock_user_struct(VERIFY_WRITE, target_tx, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    __put_user(host_tx->modes, &target_tx->modes);
    __put_user(host_tx->offset, &target_tx->offset);
    __put_user(host_tx->freq, &target_tx->freq);
    __put_user(host_tx->maxerror, &target_tx->maxerror);
    __put_user(host_tx->esterror, &target_tx->esterror);
    __put_user(host_tx->status, &target_tx->status);
    __put_user(host_tx->constant, &target_tx->constant);
    __put_user(host_tx->precision, &target_tx->precision);
    __put_user(host_tx->tolerance, &target_tx->tolerance);
    __put_user(host_tx->time.tv_sec, &target_tx->time.tv_sec);
    __put_user(host_tx->time.tv_usec, &target_tx->time.tv_usec);
    __put_user(host_tx->tick, &target_tx->tick);
    __put_user(host_tx->ppsfreq, &target_tx->ppsfreq);
    __put_user(host_tx->jitter, &target_tx->jitter);
    __put_user(host_tx->shift, &target_tx->shift);
    __put_user(host_tx->stabil, &target_tx->stabil);
    __put_user(host_tx->jitcnt, &target_tx->jitcnt);
    __put_user(host_tx->calcnt, &target_tx->calcnt);
    __put_user(host_tx->errcnt, &target_tx->errcnt);
    __put_user(host_tx->stbcnt, &target_tx->stbcnt);
    __put_user(host_tx->tai, &target_tx->tai);

    unlock_user_struct(target_tx, target_addr, 1);
    return 0;
}


static inline abi_long target_to_host_sigevent(struct sigevent *host_sevp,
                                               abi_ulong target_addr)
{
    struct target_sigevent *target_sevp;

    if (!lock_user_struct(VERIFY_READ, target_sevp, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    /* This union is awkward on 64 bit systems because it has a 32 bit
     * integer and a pointer in it; we follow the conversion approach
     * used for handling sigval types in signal.c so the guest should get
     * the correct value back even if we did a 64 bit byteswap and it's
     * using the 32 bit integer.
     */
    host_sevp->sigev_value.sival_ptr =
        (void *)(uintptr_t)tswapal(target_sevp->sigev_value.sival_ptr);
    host_sevp->sigev_signo =
        target_to_host_signal(tswap32(target_sevp->sigev_signo));
    host_sevp->sigev_notify = tswap32(target_sevp->sigev_notify);
    host_sevp->_sigev_un._tid = tswap32(target_sevp->_sigev_un._tid);

    unlock_user_struct(target_sevp, target_addr, 1);
    return 0;
}

#if (defined(TARGET_NR_stat64) || defined(TARGET_NR_lstat64) ||     \
     defined(TARGET_NR_fstat64) || defined(TARGET_NR_fstatat64) ||  \
     defined(TARGET_NR_newfstatat))
static inline abi_long host_to_target_stat64(void *cpu_env,
                                             abi_ulong target_addr,
                                             struct stat *host_st)
{
#if defined(TARGET_ARM) && defined(TARGET_ABI32)
    if (((CPUARMState *)cpu_env)->eabi) {
        struct target_eabi_stat64 *target_st;

        if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0))
            return -TARGET_EFAULT;
        memset(target_st, 0, sizeof(struct target_eabi_stat64));
        __put_user(host_st->st_dev, &target_st->st_dev);
        __put_user(host_st->st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
        __put_user(host_st->st_ino, &target_st->__st_ino);
#endif
        __put_user(host_st->st_mode, &target_st->st_mode);
        __put_user(host_st->st_nlink, &target_st->st_nlink);
        __put_user(host_st->st_uid, &target_st->st_uid);
        __put_user(host_st->st_gid, &target_st->st_gid);
        __put_user(host_st->st_rdev, &target_st->st_rdev);
        __put_user(host_st->st_size, &target_st->st_size);
        __put_user(host_st->st_blksize, &target_st->st_blksize);
        __put_user(host_st->st_blocks, &target_st->st_blocks);
        __put_user(host_st->st_atime, &target_st->target_st_atime);
        __put_user(host_st->st_mtime, &target_st->target_st_mtime);
        __put_user(host_st->st_ctime, &target_st->target_st_ctime);
        unlock_user_struct(target_st, target_addr, 1);
    } else
#endif
    {
#if defined(TARGET_HAS_STRUCT_STAT64)
        struct target_stat64 *target_st;
#else
        struct target_stat *target_st;
#endif

        if (!lock_user_struct(VERIFY_WRITE, target_st, target_addr, 0))
            return -TARGET_EFAULT;
        memset(target_st, 0, sizeof(*target_st));
        __put_user(host_st->st_dev, &target_st->st_dev);
        __put_user(host_st->st_ino, &target_st->st_ino);
#ifdef TARGET_STAT64_HAS_BROKEN_ST_INO
        __put_user(host_st->st_ino, &target_st->__st_ino);
#endif
        __put_user(host_st->st_mode, &target_st->st_mode);
        __put_user(host_st->st_nlink, &target_st->st_nlink);
        __put_user(host_st->st_uid, &target_st->st_uid);
        __put_user(host_st->st_gid, &target_st->st_gid);
        __put_user(host_st->st_rdev, &target_st->st_rdev);
        /* XXX: better use of kernel struct */
        __put_user(host_st->st_size, &target_st->st_size);
        __put_user(host_st->st_blksize, &target_st->st_blksize);
        __put_user(host_st->st_blocks, &target_st->st_blocks);
        __put_user(host_st->st_atime, &target_st->target_st_atime);
        __put_user(host_st->st_mtime, &target_st->target_st_mtime);
        __put_user(host_st->st_ctime, &target_st->target_st_ctime);
        unlock_user_struct(target_st, target_addr, 1);
    }

    return 0;
}
#endif

/* ??? Using host futex calls even when target atomic operations
   are not really atomic probably breaks things.  However implementing
   futexes locally would make futexes shared between multiple processes
   tricky.  However they're probably useless because guest atomic
   operations won't work either.  */
static int do_futex(target_ulong uaddr, int op, int val, target_ulong timeout,
                    target_ulong uaddr2, int val3)
{
    struct timespec ts, *pts;
    int base_op;

    /* ??? We assume FUTEX_* constants are the same on both host
       and target.  */
#ifdef FUTEX_CMD_MASK
    base_op = op & FUTEX_CMD_MASK;
#else
    base_op = op;
#endif
    switch (base_op) {
    case FUTEX_WAIT:
    case FUTEX_WAIT_BITSET:
        if (timeout) {
            pts = &ts;
            target_to_host_timespec(pts, timeout);
        } else {
            pts = NULL;
        }
        return get_errno(safe_futex(g2h(uaddr), op, tswap32(val),
                         pts, NULL, val3));
    case FUTEX_WAKE:
        return get_errno(safe_futex(g2h(uaddr), op, val, NULL, NULL, 0));
    case FUTEX_FD:
        return get_errno(safe_futex(g2h(uaddr), op, val, NULL, NULL, 0));
    case FUTEX_REQUEUE:
    case FUTEX_CMP_REQUEUE:
    case FUTEX_WAKE_OP:
        /* For FUTEX_REQUEUE, FUTEX_CMP_REQUEUE, and FUTEX_WAKE_OP, the
           TIMEOUT parameter is interpreted as a uint32_t by the kernel.
           But the prototype takes a `struct timespec *'; insert casts
           to satisfy the compiler.  We do not need to tswap TIMEOUT
           since it's not compared to guest memory.  */
        pts = (struct timespec *)(uintptr_t) timeout;
        return get_errno(safe_futex(g2h(uaddr), op, val, pts,
                                    g2h(uaddr2),
                                    (base_op == FUTEX_CMP_REQUEUE
                                     ? tswap32(val3)
                                     : val3)));
    default:
        return -TARGET_ENOSYS;
    }
}

#if defined(TARGET_NR_signalfd) || defined(TARGET_NR_signalfd4)

static abi_long do_signalfd4(int fd, abi_long mask, int flags)
{
    int host_flags;
    target_sigset_t *target_mask;
    sigset_t host_mask;
    abi_long ret;

    if (flags & ~(TARGET_O_NONBLOCK | TARGET_O_CLOEXEC)) {
        return -TARGET_EINVAL;
    }
    if (!lock_user_struct(VERIFY_READ, target_mask, mask, 1)) {
        return -TARGET_EFAULT;
    }

    target_to_host_sigset(&host_mask, target_mask);

    host_flags = target_to_host_bitmask(flags, fcntl_flags_tbl);

    ret = get_errno(signalfd(fd, &host_mask, host_flags));
    if (ret >= 0) {
        fd_trans_register(ret, &target_signalfd_trans);
    }

    unlock_user_struct(target_mask, mask, 0);

    return ret;
}
#endif

#define TIMER_MAGIC 0x0caf0000
#define TIMER_MAGIC_MASK 0xffff0000

/* Convert QEMU provided timer ID back to internal 16bit index format */
static target_timer_t get_timer_id(abi_long arg)
{
    target_timer_t timerid = arg;

    if ((timerid & TIMER_MAGIC_MASK) != TIMER_MAGIC) {
        return -TARGET_EINVAL;
    }

    timerid &= 0xffff;

    if (timerid >= ARRAY_SIZE(g_posix_timers)) {
        return -TARGET_EINVAL;
    }

    return timerid;
}

static int target_to_host_cpu_mask(unsigned long *host_mask,
                                   size_t host_size,
                                   abi_ulong target_addr,
                                   size_t target_size)
{
    unsigned target_bits = sizeof(abi_ulong) * 8;
    unsigned host_bits = sizeof(*host_mask) * 8;
    abi_ulong *target_mask;
    unsigned i, j;

    assert(host_size >= target_size);

    target_mask = lock_user(VERIFY_READ, target_addr, target_size, 1);
    if (!target_mask) {
        return -TARGET_EFAULT;
    }
    memset(host_mask, 0, host_size);

    for (i = 0 ; i < target_size / sizeof(abi_ulong); i++) {
        unsigned bit = i * target_bits;
        abi_ulong val;

        __get_user(val, &target_mask[i]);
        for (j = 0; j < target_bits; j++, bit++) {
            if (val & (1UL << j)) {
                host_mask[bit / host_bits] |= 1UL << (bit % host_bits);
            }
        }
    }

    unlock_user(target_mask, target_addr, 0);
    return 0;
}

static int host_to_target_cpu_mask(const unsigned long *host_mask,
                                   size_t host_size,
                                   abi_ulong target_addr,
                                   size_t target_size)
{
    unsigned target_bits = sizeof(abi_ulong) * 8;
    unsigned host_bits = sizeof(*host_mask) * 8;
    abi_ulong *target_mask;
    unsigned i, j;

    assert(host_size >= target_size);

    target_mask = lock_user(VERIFY_WRITE, target_addr, target_size, 0);
    if (!target_mask) {
        return -TARGET_EFAULT;
    }

    for (i = 0 ; i < target_size / sizeof(abi_ulong); i++) {
        unsigned bit = i * target_bits;
        abi_ulong val = 0;

        for (j = 0; j < target_bits; j++, bit++) {
            if (host_mask[bit / host_bits] & (1UL << (bit % host_bits))) {
                val |= 1UL << j;
            }
        }
        __put_user(val, &target_mask[i]);
    }

    unlock_user(target_mask, target_addr, target_size);
    return 0;
}

/* This is an internal helper for do_syscall so that it is easier
 * to have a single return point, so that actions, such as logging
 * of syscall results, can be performed.
 * All errnos that do_syscall() returns must be -TARGET_<errcode>.
 */
static abi_long do_syscall1(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    abi_long ret;
#if defined(TARGET_NR_stat) || defined(TARGET_NR_stat64) \
    || defined(TARGET_NR_lstat) || defined(TARGET_NR_lstat64) \
    || defined(TARGET_NR_fstat) || defined(TARGET_NR_fstat64)
    struct stat st;
#endif
#if defined(TARGET_NR_statfs) || defined(TARGET_NR_statfs64) \
    || defined(TARGET_NR_fstatfs)
    struct statfs stfs;
#endif
    void *p;

    switch(num) {
#ifdef TARGET_NR_sigaction
    case TARGET_NR_sigaction:
        {
#if defined(TARGET_ALPHA)
            struct target_sigaction act, oact, *pact = 0;
            struct target_old_sigaction *old_act;
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    return -TARGET_EFAULT;
                act._sa_handler = old_act->_sa_handler;
                target_siginitset(&act.sa_mask, old_act->sa_mask);
                act.sa_flags = old_act->sa_flags;
                act.sa_restorer = 0;
                unlock_user_struct(old_act, arg2, 0);
                pact = &act;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    return -TARGET_EFAULT;
                old_act->_sa_handler = oact._sa_handler;
                old_act->sa_mask = oact.sa_mask.sig[0];
                old_act->sa_flags = oact.sa_flags;
                unlock_user_struct(old_act, arg3, 1);
            }
#elif defined(TARGET_MIPS)
	    struct target_sigaction act, oact, *pact, *old_act;

	    if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    return -TARGET_EFAULT;
		act._sa_handler = old_act->_sa_handler;
		target_siginitset(&act.sa_mask, old_act->sa_mask.sig[0]);
		act.sa_flags = old_act->sa_flags;
		unlock_user_struct(old_act, arg2, 0);
		pact = &act;
	    } else {
		pact = NULL;
	    }

	    ret = get_errno(do_sigaction(arg1, pact, &oact));

	    if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    return -TARGET_EFAULT;
		old_act->_sa_handler = oact._sa_handler;
		old_act->sa_flags = oact.sa_flags;
		old_act->sa_mask.sig[0] = oact.sa_mask.sig[0];
		old_act->sa_mask.sig[1] = 0;
		old_act->sa_mask.sig[2] = 0;
		old_act->sa_mask.sig[3] = 0;
		unlock_user_struct(old_act, arg3, 1);
	    }
#else
            struct target_old_sigaction *old_act;
            struct target_sigaction act, oact, *pact;
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1))
                    return -TARGET_EFAULT;
                act._sa_handler = old_act->_sa_handler;
                target_siginitset(&act.sa_mask, old_act->sa_mask);
                act.sa_flags = old_act->sa_flags;
                act.sa_restorer = old_act->sa_restorer;
#ifdef TARGET_ARCH_HAS_KA_RESTORER
                act.ka_restorer = 0;
#endif
                unlock_user_struct(old_act, arg2, 0);
                pact = &act;
            } else {
                pact = NULL;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0))
                    return -TARGET_EFAULT;
                old_act->_sa_handler = oact._sa_handler;
                old_act->sa_mask = oact.sa_mask.sig[0];
                old_act->sa_flags = oact.sa_flags;
                old_act->sa_restorer = oact.sa_restorer;
                unlock_user_struct(old_act, arg3, 1);
            }
#endif
        }
        return ret;
#endif
    case TARGET_NR_rt_sigaction:
        {
#if defined(TARGET_ALPHA)
            /* For Alpha and SPARC this is a 5 argument syscall, with
             * a 'restorer' parameter which must be copied into the
             * sa_restorer field of the sigaction struct.
             * For Alpha that 'restorer' is arg5; for SPARC it is arg4,
             * and arg5 is the sigsetsize.
             * Alpha also has a separate rt_sigaction struct that it uses
             * here; SPARC uses the usual sigaction struct.
             */
            struct target_rt_sigaction *rt_act;
            struct target_sigaction act, oact, *pact = 0;

            if (arg4 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, rt_act, arg2, 1))
                    return -TARGET_EFAULT;
                act._sa_handler = rt_act->_sa_handler;
                act.sa_mask = rt_act->sa_mask;
                act.sa_flags = rt_act->sa_flags;
                act.sa_restorer = arg5;
                unlock_user_struct(rt_act, arg2, 0);
                pact = &act;
            }
            ret = get_errno(do_sigaction(arg1, pact, &oact));
            if (!is_error(ret) && arg3) {
                if (!lock_user_struct(VERIFY_WRITE, rt_act, arg3, 0))
                    return -TARGET_EFAULT;
                rt_act->_sa_handler = oact._sa_handler;
                rt_act->sa_mask = oact.sa_mask;
                rt_act->sa_flags = oact.sa_flags;
                unlock_user_struct(rt_act, arg3, 1);
            }
#else
#ifdef TARGET_SPARC
            target_ulong restorer = arg4;
            target_ulong sigsetsize = arg5;
#else
            target_ulong sigsetsize = arg4;
#endif
            struct target_sigaction *act;
            struct target_sigaction *oact;

            if (sigsetsize != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }
            if (arg2) {
                if (!lock_user_struct(VERIFY_READ, act, arg2, 1)) {
                    return -TARGET_EFAULT;
                }
#ifdef TARGET_ARCH_HAS_KA_RESTORER
                act->ka_restorer = restorer;
#endif
            } else {
                act = NULL;
            }
            if (arg3) {
                if (!lock_user_struct(VERIFY_WRITE, oact, arg3, 0)) {
                    ret = -TARGET_EFAULT;
                    goto rt_sigaction_fail;
                }
            } else
                oact = NULL;
            ret = get_errno(do_sigaction(arg1, act, oact));
	rt_sigaction_fail:
            if (act)
                unlock_user_struct(act, arg2, 0);
            if (oact)
                unlock_user_struct(oact, arg3, 1);
#endif
        }
        return ret;
#ifdef TARGET_NR_sgetmask /* not on alpha */
    case TARGET_NR_sgetmask:
        {
            sigset_t cur_set;
            abi_ulong target_set;
            ret = do_sigprocmask(0, NULL, &cur_set);
            if (!ret) {
                host_to_target_old_sigset(&target_set, &cur_set);
                ret = target_set;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_ssetmask /* not on alpha */
    case TARGET_NR_ssetmask:
        {
            sigset_t set, oset;
            abi_ulong target_set = arg1;
            target_to_host_old_sigset(&set, &target_set);
            ret = do_sigprocmask(SIG_SETMASK, &set, &oset);
            if (!ret) {
                host_to_target_old_sigset(&target_set, &oset);
                ret = target_set;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_sigprocmask
    case TARGET_NR_sigprocmask:
        {
#if defined(TARGET_ALPHA)
            sigset_t set, oldset;
            abi_ulong mask;
            int how;

            switch (arg1) {
            case TARGET_SIG_BLOCK:
                how = SIG_BLOCK;
                break;
            case TARGET_SIG_UNBLOCK:
                how = SIG_UNBLOCK;
                break;
            case TARGET_SIG_SETMASK:
                how = SIG_SETMASK;
                break;
            default:
                return -TARGET_EINVAL;
            }
            mask = arg2;
            target_to_host_old_sigset(&set, &mask);

            ret = do_sigprocmask(how, &set, &oldset);
            if (!is_error(ret)) {
                host_to_target_old_sigset(&mask, &oldset);
                ret = mask;
                ((CPUAlphaState *)cpu_env)->ir[IR_V0] = 0; /* force no error */
            }
#else
            sigset_t set, oldset, *set_ptr;
            int how;

            if (arg2) {
                switch (arg1) {
                case TARGET_SIG_BLOCK:
                    how = SIG_BLOCK;
                    break;
                case TARGET_SIG_UNBLOCK:
                    how = SIG_UNBLOCK;
                    break;
                case TARGET_SIG_SETMASK:
                    how = SIG_SETMASK;
                    break;
                default:
                    return -TARGET_EINVAL;
                }
                if (!(p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1)))
                    return -TARGET_EFAULT;
                target_to_host_old_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = do_sigprocmask(how, set_ptr, &oldset);
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_old_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
#endif
        }
        return ret;
#endif
    case TARGET_NR_rt_sigprocmask:
        {
            int how = arg1;
            sigset_t set, oldset, *set_ptr;

            if (arg4 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            if (arg2) {
                switch(how) {
                case TARGET_SIG_BLOCK:
                    how = SIG_BLOCK;
                    break;
                case TARGET_SIG_UNBLOCK:
                    how = SIG_UNBLOCK;
                    break;
                case TARGET_SIG_SETMASK:
                    how = SIG_SETMASK;
                    break;
                default:
                    return -TARGET_EINVAL;
                }
                if (!(p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1)))
                    return -TARGET_EFAULT;
                target_to_host_sigset(&set, p);
                unlock_user(p, arg2, 0);
                set_ptr = &set;
            } else {
                how = 0;
                set_ptr = NULL;
            }
            ret = do_sigprocmask(how, set_ptr, &oldset);
            if (!is_error(ret) && arg3) {
                if (!(p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_sigset(p, &oldset);
                unlock_user(p, arg3, sizeof(target_sigset_t));
            }
        }
        return ret;
#ifdef TARGET_NR_sigpending
    case TARGET_NR_sigpending:
        {
            sigset_t set;
            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                if (!(p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_old_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        return ret;
#endif
    case TARGET_NR_rt_sigpending:
        {
            sigset_t set;

            /* Yes, this check is >, not != like most. We follow the kernel's
             * logic and it does it like this because it implements
             * NR_sigpending through the same code path, and in that case
             * the old_sigset_t is smaller in size.
             */
            if (arg2 > sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            ret = get_errno(sigpending(&set));
            if (!is_error(ret)) {
                if (!(p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0)))
                    return -TARGET_EFAULT;
                host_to_target_sigset(p, &set);
                unlock_user(p, arg1, sizeof(target_sigset_t));
            }
        }
        return ret;
#ifdef TARGET_NR_sigsuspend
    case TARGET_NR_sigsuspend:
        {
            TaskState *ts = cpu->opaque;
#if defined(TARGET_ALPHA)
            abi_ulong mask = arg1;
            target_to_host_old_sigset(&ts->sigsuspend_mask, &mask);
#else
            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                return -TARGET_EFAULT;
            target_to_host_old_sigset(&ts->sigsuspend_mask, p);
            unlock_user(p, arg1, 0);
#endif
            ret = get_errno(safe_rt_sigsuspend(&ts->sigsuspend_mask,
                                               SIGSET_T_SIZE));
            if (ret != -TARGET_ERESTARTSYS) {
                ts->in_sigsuspend = 1;
            }
        }
        return ret;
#endif
    case TARGET_NR_rt_sigsuspend:
        {
            TaskState *ts = cpu->opaque;

            if (arg2 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }
            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                return -TARGET_EFAULT;
            target_to_host_sigset(&ts->sigsuspend_mask, p);
            unlock_user(p, arg1, 0);
            ret = get_errno(safe_rt_sigsuspend(&ts->sigsuspend_mask,
                                               SIGSET_T_SIZE));
            if (ret != -TARGET_ERESTARTSYS) {
                ts->in_sigsuspend = 1;
            }
        }
        return ret;
    case TARGET_NR_rt_sigtimedwait:
        {
            sigset_t set;
            struct timespec uts, *puts;
            siginfo_t uinfo;

            if (arg4 != sizeof(target_sigset_t)) {
                return -TARGET_EINVAL;
            }

            if (!(p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1)))
                return -TARGET_EFAULT;
            target_to_host_sigset(&set, p);
            unlock_user(p, arg1, 0);
            if (arg3) {
                puts = &uts;
                target_to_host_timespec(puts, arg3);
            } else {
                puts = NULL;
            }
            ret = get_errno(safe_rt_sigtimedwait(&set, &uinfo, puts,
                                                 SIGSET_T_SIZE));
            if (!is_error(ret)) {
                if (arg2) {
                    p = lock_user(VERIFY_WRITE, arg2, sizeof(target_siginfo_t),
                                  0);
                    if (!p) {
                        return -TARGET_EFAULT;
                    }
                    host_to_target_siginfo(p, &uinfo);
                    unlock_user(p, arg2, sizeof(target_siginfo_t));
                }
                ret = host_to_target_signal(ret);
            }
        }
        return ret;
    case TARGET_NR_rt_sigqueueinfo:
        {
            siginfo_t uinfo;

            p = lock_user(VERIFY_READ, arg3, sizeof(target_siginfo_t), 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg3, 0);
            ret = get_errno(sys_rt_sigqueueinfo(arg1, arg2, &uinfo));
        }
        return ret;
    case TARGET_NR_rt_tgsigqueueinfo:
        {
            siginfo_t uinfo;

            p = lock_user(VERIFY_READ, arg4, sizeof(target_siginfo_t), 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            target_to_host_siginfo(&uinfo, p);
            unlock_user(p, arg4, 0);
            ret = get_errno(sys_rt_tgsigqueueinfo(arg1, arg2, arg3, &uinfo));
        }
        return ret;
#ifdef TARGET_NR_sigreturn
    case TARGET_NR_sigreturn:
        if (block_signals()) {
            return -TARGET_ERESTARTSYS;
        }
        return do_sigreturn(cpu_env);
#endif
    case TARGET_NR_rt_sigreturn:
        if (block_signals()) {
            return -TARGET_ERESTARTSYS;
        }
        return do_rt_sigreturn(cpu_env);
    case TARGET_NR_sethostname:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(sethostname(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#ifdef TARGET_NR_setrlimit
    case TARGET_NR_setrlimit:
        {
            int resource = target_to_host_resource(arg1);
            struct target_rlimit *target_rlim;
            struct rlimit rlim;
            if (!lock_user_struct(VERIFY_READ, target_rlim, arg2, 1))
                return -TARGET_EFAULT;
            rlim.rlim_cur = target_to_host_rlim(target_rlim->rlim_cur);
            rlim.rlim_max = target_to_host_rlim(target_rlim->rlim_max);
            unlock_user_struct(target_rlim, arg2, 0);
            /*
             * If we just passed through resource limit settings for memory then
             * they would also apply to QEMU's own allocations, and QEMU will
             * crash or hang or die if its allocations fail. Ideally we would
             * track the guest allocations in QEMU and apply the limits ourselves.
             * For now, just tell the guest the call succeeded but don't actually
             * limit anything.
             */
            if (resource != RLIMIT_AS &&
                resource != RLIMIT_DATA &&
                resource != RLIMIT_STACK) {
                return get_errno(setrlimit(resource, &rlim));
            } else {
                return 0;
            }
        }
#endif
#ifdef TARGET_NR_getrlimit
    case TARGET_NR_getrlimit:
        {
            int resource = target_to_host_resource(arg1);
            struct target_rlimit *target_rlim;
            struct rlimit rlim;

            ret = get_errno(getrlimit(resource, &rlim));
            if (!is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                    return -TARGET_EFAULT;
                target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
                target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
                unlock_user_struct(target_rlim, arg2, 1);
            }
        }
        return ret;
#endif
    case TARGET_NR_getrusage:
        {
            struct rusage rusage;
            ret = get_errno(getrusage(arg1, &rusage));
            if (!is_error(ret)) {
                ret = host_to_target_rusage(arg2, &rusage);
            }
        }
        return ret;
    case TARGET_NR_gettimeofday:
        {
            struct timeval tv;
            ret = get_errno(gettimeofday(&tv, NULL));
            if (!is_error(ret)) {
                if (copy_to_user_timeval(arg1, &tv))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
    case TARGET_NR_settimeofday:
        {
            struct timeval tv, *ptv = NULL;
            struct timezone tz, *ptz = NULL;

            if (arg1) {
                if (copy_from_user_timeval(&tv, arg1)) {
                    return -TARGET_EFAULT;
                }
                ptv = &tv;
            }

            if (arg2) {
                if (copy_from_user_timezone(&tz, arg2)) {
                    return -TARGET_EFAULT;
                }
                ptz = &tz;
            }

            return get_errno(settimeofday(ptv, ptz));
        }
#if defined(TARGET_NR_select)
    case TARGET_NR_select:
#if defined(TARGET_WANT_NI_OLD_SELECT)
        /* some architectures used to have old_select here
         * but now ENOSYS it.
         */
        ret = -TARGET_ENOSYS;
#elif defined(TARGET_WANT_OLD_SYS_SELECT)
        ret = do_old_select(arg1);
#else
        ret = do_select(arg1, arg2, arg3, arg4, arg5);
#endif
        return ret;
#endif
#ifdef TARGET_NR_pselect6
    case TARGET_NR_pselect6:
        {
            abi_long rfd_addr, wfd_addr, efd_addr, n, ts_addr;
            fd_set rfds, wfds, efds;
            fd_set *rfds_ptr, *wfds_ptr, *efds_ptr;
            struct timespec ts, *ts_ptr;

            /*
             * The 6th arg is actually two args smashed together,
             * so we cannot use the C library.
             */
            sigset_t set;
            struct {
                sigset_t *set;
                size_t size;
            } sig, *sig_ptr;

            abi_ulong arg_sigset, arg_sigsize, *arg7;
            target_sigset_t *target_sigset;

            n = arg1;
            rfd_addr = arg2;
            wfd_addr = arg3;
            efd_addr = arg4;
            ts_addr = arg5;

            ret = copy_from_user_fdset_ptr(&rfds, &rfds_ptr, rfd_addr, n);
            if (ret) {
                return ret;
            }
            ret = copy_from_user_fdset_ptr(&wfds, &wfds_ptr, wfd_addr, n);
            if (ret) {
                return ret;
            }
            ret = copy_from_user_fdset_ptr(&efds, &efds_ptr, efd_addr, n);
            if (ret) {
                return ret;
            }

            /*
             * This takes a timespec, and not a timeval, so we cannot
             * use the do_select() helper ...
             */
            if (ts_addr) {
                if (target_to_host_timespec(&ts, ts_addr)) {
                    return -TARGET_EFAULT;
                }
                ts_ptr = &ts;
            } else {
                ts_ptr = NULL;
            }

            /* Extract the two packed args for the sigset */
            if (arg6) {
                sig_ptr = &sig;
                sig.size = SIGSET_T_SIZE;

                arg7 = lock_user(VERIFY_READ, arg6, sizeof(*arg7) * 2, 1);
                if (!arg7) {
                    return -TARGET_EFAULT;
                }
                arg_sigset = tswapal(arg7[0]);
                arg_sigsize = tswapal(arg7[1]);
                unlock_user(arg7, arg6, 0);

                if (arg_sigset) {
                    sig.set = &set;
                    if (arg_sigsize != sizeof(*target_sigset)) {
                        /* Like the kernel, we enforce correct size sigsets */
                        return -TARGET_EINVAL;
                    }
                    target_sigset = lock_user(VERIFY_READ, arg_sigset,
                                              sizeof(*target_sigset), 1);
                    if (!target_sigset) {
                        return -TARGET_EFAULT;
                    }
                    target_to_host_sigset(&set, target_sigset);
                    unlock_user(target_sigset, arg_sigset, 0);
                } else {
                    sig.set = NULL;
                }
            } else {
                sig_ptr = NULL;
            }

            ret = get_errno(safe_pselect6(n, rfds_ptr, wfds_ptr, efds_ptr,
                                          ts_ptr, sig_ptr));

            if (!is_error(ret)) {
                if (rfd_addr && copy_to_user_fdset(rfd_addr, &rfds, n))
                    return -TARGET_EFAULT;
                if (wfd_addr && copy_to_user_fdset(wfd_addr, &wfds, n))
                    return -TARGET_EFAULT;
                if (efd_addr && copy_to_user_fdset(efd_addr, &efds, n))
                    return -TARGET_EFAULT;

                if (ts_addr && host_to_target_timespec(ts_addr, &ts))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_symlink
    case TARGET_NR_symlink:
        {
            void *p2;
            p = lock_user_string(arg1);
            p2 = lock_user_string(arg2);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(symlink(p, p2));
            unlock_user(p2, arg2, 0);
            unlock_user(p, arg1, 0);
        }
        return ret;
#endif
#if defined(TARGET_NR_symlinkat)
    case TARGET_NR_symlinkat:
        {
            void *p2;
            p  = lock_user_string(arg1);
            p2 = lock_user_string(arg3);
            if (!p || !p2)
                ret = -TARGET_EFAULT;
            else
                ret = get_errno(symlinkat(p, arg2, p2));
            unlock_user(p2, arg3, 0);
            unlock_user(p, arg1, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_swapon
    case TARGET_NR_swapon:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(swapon(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_reboot:
        if (arg3 == LINUX_REBOOT_CMD_RESTART2) {
           /* arg4 must be ignored in all other cases */
           p = lock_user_string(arg4);
           if (!p) {
               return -TARGET_EFAULT;
           }
           ret = get_errno(reboot(arg1, arg2, arg3, p));
           unlock_user(p, arg4, 0);
        } else {
           ret = get_errno(reboot(arg1, arg2, arg3, NULL));
        }
        return ret;
#ifdef TARGET_NR_truncate
    case TARGET_NR_truncate:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(truncate(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_ftruncate
    case TARGET_NR_ftruncate:
        return get_errno(ftruncate(arg1, arg2));
#endif
    case TARGET_NR_getpriority:
        /* Note that negative values are valid for getpriority, so we must
           differentiate based on errno settings.  */
        errno = 0;
        ret = getpriority(arg1, arg2);
        if (ret == -1 && errno != 0) {
            return -host_to_target_errno(errno);
        }
#ifdef TARGET_ALPHA
        /* Return value is the unbiased priority.  Signal no error.  */
        ((CPUAlphaState *)cpu_env)->ir[IR_V0] = 0;
#else
        /* Return value is a biased priority to avoid negative numbers.  */
        ret = 20 - ret;
#endif
        return ret;
    case TARGET_NR_setpriority:
        return get_errno(setpriority(arg1, arg2, arg3));
#ifdef TARGET_NR_statfs
    case TARGET_NR_statfs:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs:
        if (!is_error(ret)) {
            struct target_statfs *target_stfs;

            if (!lock_user_struct(VERIFY_WRITE, target_stfs, arg2, 0))
                return -TARGET_EFAULT;
            __put_user(stfs.f_type, &target_stfs->f_type);
            __put_user(stfs.f_bsize, &target_stfs->f_bsize);
            __put_user(stfs.f_blocks, &target_stfs->f_blocks);
            __put_user(stfs.f_bfree, &target_stfs->f_bfree);
            __put_user(stfs.f_bavail, &target_stfs->f_bavail);
            __put_user(stfs.f_files, &target_stfs->f_files);
            __put_user(stfs.f_ffree, &target_stfs->f_ffree);
            __put_user(stfs.f_fsid.__val[0], &target_stfs->f_fsid.val[0]);
            __put_user(stfs.f_fsid.__val[1], &target_stfs->f_fsid.val[1]);
            __put_user(stfs.f_namelen, &target_stfs->f_namelen);
            __put_user(stfs.f_frsize, &target_stfs->f_frsize);
#ifdef _STATFS_F_FLAGS
            __put_user(stfs.f_flags, &target_stfs->f_flags);
#else
            __put_user(0, &target_stfs->f_flags);
#endif
            memset(target_stfs->f_spare, 0, sizeof(target_stfs->f_spare));
            unlock_user_struct(target_stfs, arg2, 1);
        }
        return ret;
#endif
#ifdef TARGET_NR_fstatfs
    case TARGET_NR_fstatfs:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs;
#endif
#ifdef TARGET_NR_statfs64
    case TARGET_NR_statfs64:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(statfs(path(p), &stfs));
        unlock_user(p, arg1, 0);
    convert_statfs64:
        if (!is_error(ret)) {
            struct target_statfs64 *target_stfs;

            if (!lock_user_struct(VERIFY_WRITE, target_stfs, arg3, 0))
                return -TARGET_EFAULT;
            __put_user(stfs.f_type, &target_stfs->f_type);
            __put_user(stfs.f_bsize, &target_stfs->f_bsize);
            __put_user(stfs.f_blocks, &target_stfs->f_blocks);
            __put_user(stfs.f_bfree, &target_stfs->f_bfree);
            __put_user(stfs.f_bavail, &target_stfs->f_bavail);
            __put_user(stfs.f_files, &target_stfs->f_files);
            __put_user(stfs.f_ffree, &target_stfs->f_ffree);
            __put_user(stfs.f_fsid.__val[0], &target_stfs->f_fsid.val[0]);
            __put_user(stfs.f_fsid.__val[1], &target_stfs->f_fsid.val[1]);
            __put_user(stfs.f_namelen, &target_stfs->f_namelen);
            __put_user(stfs.f_frsize, &target_stfs->f_frsize);
            memset(target_stfs->f_spare, 0, sizeof(target_stfs->f_spare));
            unlock_user_struct(target_stfs, arg3, 1);
        }
        return ret;
    case TARGET_NR_fstatfs64:
        ret = get_errno(fstatfs(arg1, &stfs));
        goto convert_statfs64;
#endif
#ifdef TARGET_NR_socketcall
    case TARGET_NR_socketcall:
        return do_socketcall(arg1, arg2);
#endif
#ifdef TARGET_NR_accept
    case TARGET_NR_accept:
        return do_accept4(arg1, arg2, arg3, 0);
#endif
#ifdef TARGET_NR_accept4
    case TARGET_NR_accept4:
        return do_accept4(arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_bind
    case TARGET_NR_bind:
        return do_bind(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_connect
    case TARGET_NR_connect:
        return do_connect(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_getpeername
    case TARGET_NR_getpeername:
        return do_getpeername(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_getsockname
    case TARGET_NR_getsockname:
        return do_getsockname(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_getsockopt
    case TARGET_NR_getsockopt:
        return do_getsockopt(arg1, arg2, arg3, arg4, arg5);
#endif
#ifdef TARGET_NR_listen
    case TARGET_NR_listen:
        return get_errno(listen(arg1, arg2));
#endif
#ifdef TARGET_NR_recv
    case TARGET_NR_recv:
        return do_recvfrom(arg1, arg2, arg3, arg4, 0, 0);
#endif
#ifdef TARGET_NR_recvfrom
    case TARGET_NR_recvfrom:
        return do_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef TARGET_NR_recvmsg
    case TARGET_NR_recvmsg:
        return do_sendrecvmsg(arg1, arg2, arg3, 0);
#endif
#ifdef TARGET_NR_send
    case TARGET_NR_send:
        return do_sendto(arg1, arg2, arg3, arg4, 0, 0);
#endif
#ifdef TARGET_NR_sendmsg
    case TARGET_NR_sendmsg:
        return do_sendrecvmsg(arg1, arg2, arg3, 1);
#endif
#ifdef TARGET_NR_sendmmsg
    case TARGET_NR_sendmmsg:
        return do_sendrecvmmsg(arg1, arg2, arg3, arg4, 1);
    case TARGET_NR_recvmmsg:
        return do_sendrecvmmsg(arg1, arg2, arg3, arg4, 0);
#endif
#ifdef TARGET_NR_sendto
    case TARGET_NR_sendto:
        return do_sendto(arg1, arg2, arg3, arg4, arg5, arg6);
#endif
#ifdef TARGET_NR_shutdown
    case TARGET_NR_shutdown:
        return get_errno(shutdown(arg1, arg2));
#endif
#if defined(TARGET_NR_getrandom) && defined(__NR_getrandom)
    case TARGET_NR_getrandom:
        p = lock_user(VERIFY_WRITE, arg1, arg2, 0);
        if (!p) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(getrandom(p, arg2, arg3));
        unlock_user(p, arg1, ret);
        return ret;
#endif
#ifdef TARGET_NR_socket
    case TARGET_NR_socket:
        return do_socket(arg1, arg2, arg3);
#endif
#ifdef TARGET_NR_socketpair
    case TARGET_NR_socketpair:
        return do_socketpair(arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_setsockopt
    case TARGET_NR_setsockopt:
        return do_setsockopt(arg1, arg2, arg3, arg4, (socklen_t) arg5);
#endif
#if defined(TARGET_NR_syslog)
    case TARGET_NR_syslog:
        {
            int len = arg2;

            switch (arg1) {
            case TARGET_SYSLOG_ACTION_CLOSE:         /* Close log */
            case TARGET_SYSLOG_ACTION_OPEN:          /* Open log */
            case TARGET_SYSLOG_ACTION_CLEAR:         /* Clear ring buffer */
            case TARGET_SYSLOG_ACTION_CONSOLE_OFF:   /* Disable logging */
            case TARGET_SYSLOG_ACTION_CONSOLE_ON:    /* Enable logging */
            case TARGET_SYSLOG_ACTION_CONSOLE_LEVEL: /* Set messages level */
            case TARGET_SYSLOG_ACTION_SIZE_UNREAD:   /* Number of chars */
            case TARGET_SYSLOG_ACTION_SIZE_BUFFER:   /* Size of the buffer */
                return get_errno(sys_syslog((int)arg1, NULL, (int)arg3));
            case TARGET_SYSLOG_ACTION_READ:          /* Read from log */
            case TARGET_SYSLOG_ACTION_READ_CLEAR:    /* Read/clear msgs */
            case TARGET_SYSLOG_ACTION_READ_ALL:      /* Read last messages */
                {
                    if (len < 0) {
                        return -TARGET_EINVAL;
                    }
                    if (len == 0) {
                        return 0;
                    }
                    p = lock_user(VERIFY_WRITE, arg2, arg3, 0);
                    if (!p) {
                        return -TARGET_EFAULT;
                    }
                    ret = get_errno(sys_syslog((int)arg1, p, (int)arg3));
                    unlock_user(p, arg2, arg3);
                }
                return ret;
            default:
                return -TARGET_EINVAL;
            }
        }
        break;
#endif
    case TARGET_NR_setitimer:
        {
            struct itimerval value, ovalue, *pvalue;

            if (arg2) {
                pvalue = &value;
                if (copy_from_user_timeval(&pvalue->it_interval, arg2)
                    || copy_from_user_timeval(&pvalue->it_value,
                                              arg2 + sizeof(struct target_timeval)))
                    return -TARGET_EFAULT;
            } else {
                pvalue = NULL;
            }
            ret = get_errno(setitimer(arg1, pvalue, &ovalue));
            if (!is_error(ret) && arg3) {
                if (copy_to_user_timeval(arg3,
                                         &ovalue.it_interval)
                    || copy_to_user_timeval(arg3 + sizeof(struct target_timeval),
                                            &ovalue.it_value))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
    case TARGET_NR_getitimer:
        {
            struct itimerval value;

            ret = get_errno(getitimer(arg1, &value));
            if (!is_error(ret) && arg2) {
                if (copy_to_user_timeval(arg2,
                                         &value.it_interval)
                    || copy_to_user_timeval(arg2 + sizeof(struct target_timeval),
                                            &value.it_value))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#ifdef TARGET_NR_stat
    case TARGET_NR_stat:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
#endif
#ifdef TARGET_NR_lstat
    case TARGET_NR_lstat:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        goto do_stat;
#endif
#ifdef TARGET_NR_fstat
    case TARGET_NR_fstat:
        {
            ret = get_errno(fstat(arg1, &st));
#if defined(TARGET_NR_stat) || defined(TARGET_NR_lstat)
        do_stat:
#endif
            if (!is_error(ret)) {
                struct target_stat *target_st;

                if (!lock_user_struct(VERIFY_WRITE, target_st, arg2, 0))
                    return -TARGET_EFAULT;
                memset(target_st, 0, sizeof(*target_st));
                __put_user(st.st_dev, &target_st->st_dev);
                __put_user(st.st_ino, &target_st->st_ino);
                __put_user(st.st_mode, &target_st->st_mode);
                __put_user(st.st_uid, &target_st->st_uid);
                __put_user(st.st_gid, &target_st->st_gid);
                __put_user(st.st_nlink, &target_st->st_nlink);
                __put_user(st.st_rdev, &target_st->st_rdev);
                __put_user(st.st_size, &target_st->st_size);
                __put_user(st.st_blksize, &target_st->st_blksize);
                __put_user(st.st_blocks, &target_st->st_blocks);
                __put_user(st.st_atime, &target_st->target_st_atime);
                __put_user(st.st_mtime, &target_st->target_st_mtime);
                __put_user(st.st_ctime, &target_st->target_st_ctime);
                unlock_user_struct(target_st, arg2, 1);
            }
        }
        return ret;
#endif
    case TARGET_NR_vhangup:
        return get_errno(vhangup());
#ifdef TARGET_NR_syscall
    case TARGET_NR_syscall:
        return do_syscall(cpu_env, arg1 & 0xffff, arg2, arg3, arg4, arg5,
                          arg6, arg7, arg8, 0);
#endif
#ifdef TARGET_NR_swapoff
    case TARGET_NR_swapoff:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(swapoff(p));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_sysinfo:
        {
            struct target_sysinfo *target_value;
            struct sysinfo value;
            ret = get_errno(sysinfo(&value));
            if (!is_error(ret) && arg1)
            {
                if (!lock_user_struct(VERIFY_WRITE, target_value, arg1, 0))
                    return -TARGET_EFAULT;
                __put_user(value.uptime, &target_value->uptime);
                __put_user(value.loads[0], &target_value->loads[0]);
                __put_user(value.loads[1], &target_value->loads[1]);
                __put_user(value.loads[2], &target_value->loads[2]);
                __put_user(value.totalram, &target_value->totalram);
                __put_user(value.freeram, &target_value->freeram);
                __put_user(value.sharedram, &target_value->sharedram);
                __put_user(value.bufferram, &target_value->bufferram);
                __put_user(value.totalswap, &target_value->totalswap);
                __put_user(value.freeswap, &target_value->freeswap);
                __put_user(value.procs, &target_value->procs);
                __put_user(value.totalhigh, &target_value->totalhigh);
                __put_user(value.freehigh, &target_value->freehigh);
                __put_user(value.mem_unit, &target_value->mem_unit);
                unlock_user_struct(target_value, arg1, 1);
            }
        }
        return ret;
    case TARGET_NR_fsync:
        return get_errno(fsync(arg1));
#ifdef __NR_exit_group
        /* new thread calls */
    case TARGET_NR_exit_group:
        preexit_cleanup(cpu_env, arg1);
        return get_errno(exit_group(arg1));
#endif
    case TARGET_NR_setdomainname:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(setdomainname(p, arg2));
        unlock_user(p, arg1, 0);
        return ret;
    case TARGET_NR_uname:
        /* no need to transcode because we use the linux syscall */
        {
            struct new_utsname * buf;

            if (!lock_user_struct(VERIFY_WRITE, buf, arg1, 0))
                return -TARGET_EFAULT;
            ret = get_errno(sys_uname(buf));
            if (!is_error(ret)) {
                /* Overwrite the native machine name with whatever is being
                   emulated. */
                g_strlcpy(buf->machine, cpu_to_uname_machine(cpu_env),
                          sizeof(buf->machine));
                /* Allow the user to override the reported release.  */
                if (qemu_uname_release && *qemu_uname_release) {
                    g_strlcpy(buf->release, qemu_uname_release,
                              sizeof(buf->release));
                }
            }
            unlock_user_struct(buf, arg1, 1);
        }
        return ret;
#ifdef TARGET_I386
    case TARGET_NR_modify_ldt:
        return do_modify_ldt(cpu_env, arg1, arg2, arg3);
#if !defined(TARGET_X86_64)
    case TARGET_NR_vm86:
        return do_vm86(cpu_env, arg1, arg2);
#endif
#endif
    case TARGET_NR_adjtimex:
        {
            struct timex host_buf;

            if (target_to_host_timex(&host_buf, arg1) != 0) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(adjtimex(&host_buf));
            if (!is_error(ret)) {
                if (host_to_target_timex(arg1, &host_buf) != 0) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
#if defined(TARGET_NR_clock_adjtime) && defined(CONFIG_CLOCK_ADJTIME)
    case TARGET_NR_clock_adjtime:
        {
            struct timex htx, *phtx = &htx;

            if (target_to_host_timex(phtx, arg2) != 0) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(clock_adjtime(arg1, phtx));
            if (!is_error(ret) && phtx) {
                if (host_to_target_timex(arg2, phtx) != 0) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
#endif
    case TARGET_NR_fchdir:
        return get_errno(fchdir(arg1));
    case TARGET_NR_personality:
        return get_errno(personality(arg1));
#ifdef TARGET_NR_getdents
    case TARGET_NR_getdents:
#ifdef EMULATE_GETDENTS_WITH_GETDENTS
#if TARGET_ABI_BITS == 32 && HOST_LONG_BITS == 64
        {
            struct target_dirent *target_dirp;
            struct linux_dirent *dirp;
            abi_long count = arg3;

            dirp = g_try_malloc(count);
            if (!dirp) {
                return -TARGET_ENOMEM;
            }

            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct linux_dirent *de;
		struct target_dirent *tde;
                int len = ret;
                int reclen, treclen;
		int count1, tnamelen;

		count1 = 0;
                de = dirp;
                if (!(target_dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                    return -TARGET_EFAULT;
		tde = target_dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
                    tnamelen = reclen - offsetof(struct linux_dirent, d_name);
                    assert(tnamelen >= 0);
                    treclen = tnamelen + offsetof(struct target_dirent, d_name);
                    assert(count1 + treclen <= count);
                    tde->d_reclen = tswap16(treclen);
                    tde->d_ino = tswapal(de->d_ino);
                    tde->d_off = tswapal(de->d_off);
                    memcpy(tde->d_name, de->d_name, tnamelen);
                    de = (struct linux_dirent *)((char *)de + reclen);
                    len -= reclen;
                    tde = (struct target_dirent *)((char *)tde + treclen);
		    count1 += treclen;
                }
		ret = count1;
                unlock_user(target_dirp, arg2, ret);
            }
            g_free(dirp);
        }
#else
        {
            struct linux_dirent *dirp;
            abi_long count = arg3;

            if (!(dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                return -TARGET_EFAULT;
            ret = get_errno(sys_getdents(arg1, dirp, count));
            if (!is_error(ret)) {
                struct linux_dirent *de;
                int len = ret;
                int reclen;
                de = dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
                    if (reclen > len)
                        break;
                    de->d_reclen = tswap16(reclen);
                    tswapls(&de->d_ino);
                    tswapls(&de->d_off);
                    de = (struct linux_dirent *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
#endif
#else
        /* Implement getdents in terms of getdents64 */
        {
            struct linux_dirent64 *dirp;
            abi_long count = arg3;

            dirp = lock_user(VERIFY_WRITE, arg2, count, 0);
            if (!dirp) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(sys_getdents64(arg1, dirp, count));
            if (!is_error(ret)) {
                /* Convert the dirent64 structs to target dirent.  We do this
                 * in-place, since we can guarantee that a target_dirent is no
                 * larger than a dirent64; however this means we have to be
                 * careful to read everything before writing in the new format.
                 */
                struct linux_dirent64 *de;
                struct target_dirent *tde;
                int len = ret;
                int tlen = 0;

                de = dirp;
                tde = (struct target_dirent *)dirp;
                while (len > 0) {
                    int namelen, treclen;
                    int reclen = de->d_reclen;
                    uint64_t ino = de->d_ino;
                    int64_t off = de->d_off;
                    uint8_t type = de->d_type;

                    namelen = strlen(de->d_name);
                    treclen = offsetof(struct target_dirent, d_name)
                        + namelen + 2;
                    treclen = QEMU_ALIGN_UP(treclen, sizeof(abi_long));

                    memmove(tde->d_name, de->d_name, namelen + 1);
                    tde->d_ino = tswapal(ino);
                    tde->d_off = tswapal(off);
                    tde->d_reclen = tswap16(treclen);
                    /* The target_dirent type is in what was formerly a padding
                     * byte at the end of the structure:
                     */
                    *(((char *)tde) + treclen - 1) = type;

                    de = (struct linux_dirent64 *)((char *)de + reclen);
                    tde = (struct target_dirent *)((char *)tde + treclen);
                    len -= reclen;
                    tlen += treclen;
                }
                ret = tlen;
            }
            unlock_user(dirp, arg2, ret);
        }
#endif
        return ret;
#endif /* TARGET_NR_getdents */
#if defined(TARGET_NR_getdents64) && defined(__NR_getdents64)
    case TARGET_NR_getdents64:
        {
            struct linux_dirent64 *dirp;
            abi_long count = arg3;
            if (!(dirp = lock_user(VERIFY_WRITE, arg2, count, 0)))
                return -TARGET_EFAULT;
            ret = get_errno(sys_getdents64(arg1, dirp, count));
            if (!is_error(ret)) {
                struct linux_dirent64 *de;
                int len = ret;
                int reclen;
                de = dirp;
                while (len > 0) {
                    reclen = de->d_reclen;
                    if (reclen > len)
                        break;
                    de->d_reclen = tswap16(reclen);
                    tswap64s((uint64_t *)&de->d_ino);
                    tswap64s((uint64_t *)&de->d_off);
                    de = (struct linux_dirent64 *)((char *)de + reclen);
                    len -= reclen;
                }
            }
            unlock_user(dirp, arg2, ret);
        }
        return ret;
#endif /* TARGET_NR_getdents64 */
#if defined(TARGET_NR__newselect)
    case TARGET_NR__newselect:
        return do_select(arg1, arg2, arg3, arg4, arg5);
#endif
#if defined(TARGET_NR_poll) || defined(TARGET_NR_ppoll)
# ifdef TARGET_NR_poll
    case TARGET_NR_poll:
# endif
# ifdef TARGET_NR_ppoll
    case TARGET_NR_ppoll:
# endif
        {
            struct target_pollfd *target_pfd;
            unsigned int nfds = arg2;
            struct pollfd *pfd;
            unsigned int i;

            pfd = NULL;
            target_pfd = NULL;
            if (nfds) {
                if (nfds > (INT_MAX / sizeof(struct target_pollfd))) {
                    return -TARGET_EINVAL;
                }

                target_pfd = lock_user(VERIFY_WRITE, arg1,
                                       sizeof(struct target_pollfd) * nfds, 1);
                if (!target_pfd) {
                    return -TARGET_EFAULT;
                }

                pfd = alloca(sizeof(struct pollfd) * nfds);
                for (i = 0; i < nfds; i++) {
                    pfd[i].fd = tswap32(target_pfd[i].fd);
                    pfd[i].events = tswap16(target_pfd[i].events);
                }
            }

            switch (num) {
# ifdef TARGET_NR_ppoll
            case TARGET_NR_ppoll:
            {
                struct timespec _timeout_ts, *timeout_ts = &_timeout_ts;
                target_sigset_t *target_set;
                sigset_t _set, *set = &_set;

                if (arg3) {
                    if (target_to_host_timespec(timeout_ts, arg3)) {
                        unlock_user(target_pfd, arg1, 0);
                        return -TARGET_EFAULT;
                    }
                } else {
                    timeout_ts = NULL;
                }

                if (arg4) {
                    if (arg5 != sizeof(target_sigset_t)) {
                        unlock_user(target_pfd, arg1, 0);
                        return -TARGET_EINVAL;
                    }

                    target_set = lock_user(VERIFY_READ, arg4, sizeof(target_sigset_t), 1);
                    if (!target_set) {
                        unlock_user(target_pfd, arg1, 0);
                        return -TARGET_EFAULT;
                    }
                    target_to_host_sigset(set, target_set);
                } else {
                    set = NULL;
                }

                ret = get_errno(safe_ppoll(pfd, nfds, timeout_ts,
                                           set, SIGSET_T_SIZE));

                if (!is_error(ret) && arg3) {
                    host_to_target_timespec(arg3, timeout_ts);
                }
                if (arg4) {
                    unlock_user(target_set, arg4, 0);
                }
                break;
            }
# endif
# ifdef TARGET_NR_poll
            case TARGET_NR_poll:
            {
                struct timespec ts, *pts;

                if (arg3 >= 0) {
                    /* Convert ms to secs, ns */
                    ts.tv_sec = arg3 / 1000;
                    ts.tv_nsec = (arg3 % 1000) * 1000000LL;
                    pts = &ts;
                } else {
                    /* -ve poll() timeout means "infinite" */
                    pts = NULL;
                }
                ret = get_errno(safe_ppoll(pfd, nfds, pts, NULL, 0));
                break;
            }
# endif
            default:
                g_assert_not_reached();
            }

            if (!is_error(ret)) {
                for(i = 0; i < nfds; i++) {
                    target_pfd[i].revents = tswap16(pfd[i].revents);
                }
            }
            unlock_user(target_pfd, arg1, sizeof(struct target_pollfd) * nfds);
        }
        return ret;
#endif
    case TARGET_NR_flock:
        /* NOTE: the flock constant seems to be the same for every
           Linux platform */
        return get_errno(safe_flock(arg1, arg2));
#if defined(TARGET_NR_fdatasync) /* Not on alpha (osf_datasync ?) */
    case TARGET_NR_fdatasync:
        return get_errno(fdatasync(arg1));
#endif
#ifdef TARGET_NR__sysctl
    case TARGET_NR__sysctl:
        /* We don't implement this, but ENOTDIR is always a safe
           return value. */
        return -TARGET_ENOTDIR;
#endif
    case TARGET_NR_sched_getaffinity:
        {
            unsigned int mask_size;
            unsigned long *mask;

            /*
             * sched_getaffinity needs multiples of ulong, so need to take
             * care of mismatches between target ulong and host ulong sizes.
             */
            if (arg2 & (sizeof(abi_ulong) - 1)) {
                return -TARGET_EINVAL;
            }
            mask_size = (arg2 + (sizeof(*mask) - 1)) & ~(sizeof(*mask) - 1);

            mask = alloca(mask_size);
            memset(mask, 0, mask_size);
            ret = get_errno(sys_sched_getaffinity(arg1, mask_size, mask));

            if (!is_error(ret)) {
                if (ret > arg2) {
                    /* More data returned than the caller's buffer will fit.
                     * This only happens if sizeof(abi_long) < sizeof(long)
                     * and the caller passed us a buffer holding an odd number
                     * of abi_longs. If the host kernel is actually using the
                     * extra 4 bytes then fail EINVAL; otherwise we can just
                     * ignore them and only copy the interesting part.
                     */
                    int numcpus = sysconf(_SC_NPROCESSORS_CONF);
                    if (numcpus > arg2 * 8) {
                        return -TARGET_EINVAL;
                    }
                    ret = arg2;
                }

                if (host_to_target_cpu_mask(mask, mask_size, arg3, ret)) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
    case TARGET_NR_sched_setaffinity:
        {
            unsigned int mask_size;
            unsigned long *mask;

            /*
             * sched_setaffinity needs multiples of ulong, so need to take
             * care of mismatches between target ulong and host ulong sizes.
             */
            if (arg2 & (sizeof(abi_ulong) - 1)) {
                return -TARGET_EINVAL;
            }
            mask_size = (arg2 + (sizeof(*mask) - 1)) & ~(sizeof(*mask) - 1);
            mask = alloca(mask_size);

            ret = target_to_host_cpu_mask(mask, mask_size, arg3, arg2);
            if (ret) {
                return ret;
            }

            return get_errno(sys_sched_setaffinity(arg1, mask_size, mask));
        }
    case TARGET_NR_getcpu:
        {
            unsigned cpu, node;
            ret = get_errno(sys_getcpu(arg1 ? &cpu : NULL,
                                       arg2 ? &node : NULL,
                                       NULL));
            if (is_error(ret)) {
                return ret;
            }
            if (arg1 && put_user_u32(cpu, arg1)) {
                return -TARGET_EFAULT;
            }
            if (arg2 && put_user_u32(node, arg2)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
    case TARGET_NR_sched_setparam:
        {
            struct sched_param *target_schp;
            struct sched_param schp;

            if (arg2 == 0) {
                return -TARGET_EINVAL;
            }
            if (!lock_user_struct(VERIFY_READ, target_schp, arg2, 1))
                return -TARGET_EFAULT;
            schp.sched_priority = tswap32(target_schp->sched_priority);
            unlock_user_struct(target_schp, arg2, 0);
            return get_errno(sched_setparam(arg1, &schp));
        }
    case TARGET_NR_sched_getparam:
        {
            struct sched_param *target_schp;
            struct sched_param schp;

            if (arg2 == 0) {
                return -TARGET_EINVAL;
            }
            ret = get_errno(sched_getparam(arg1, &schp));
            if (!is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_schp, arg2, 0))
                    return -TARGET_EFAULT;
                target_schp->sched_priority = tswap32(schp.sched_priority);
                unlock_user_struct(target_schp, arg2, 1);
            }
        }
        return ret;
    case TARGET_NR_sched_setscheduler:
        {
            struct sched_param *target_schp;
            struct sched_param schp;
            if (arg3 == 0) {
                return -TARGET_EINVAL;
            }
            if (!lock_user_struct(VERIFY_READ, target_schp, arg3, 1))
                return -TARGET_EFAULT;
            schp.sched_priority = tswap32(target_schp->sched_priority);
            unlock_user_struct(target_schp, arg3, 0);
            return get_errno(sched_setscheduler(arg1, arg2, &schp));
        }
    case TARGET_NR_sched_getscheduler:
        return get_errno(sched_getscheduler(arg1));
    case TARGET_NR_sched_yield:
        return get_errno(sched_yield());
    case TARGET_NR_sched_get_priority_max:
        return get_errno(sched_get_priority_max(arg1));
    case TARGET_NR_sched_get_priority_min:
        return get_errno(sched_get_priority_min(arg1));
    case TARGET_NR_sched_rr_get_interval:
        {
            struct timespec ts;
            ret = get_errno(sched_rr_get_interval(arg1, &ts));
            if (!is_error(ret)) {
                ret = host_to_target_timespec(arg2, &ts);
            }
        }
        return ret;
    case TARGET_NR_nanosleep:
        {
            struct timespec req, rem;
            target_to_host_timespec(&req, arg1);
            ret = get_errno(safe_nanosleep(&req, &rem));
            if (is_error(ret) && arg2) {
                host_to_target_timespec(arg2, &rem);
            }
        }
        return ret;
    case TARGET_NR_prctl:
        switch (arg1) {
        case PR_GET_PDEATHSIG:
        {
            int deathsig;
            ret = get_errno(prctl(arg1, &deathsig, arg3, arg4, arg5));
            if (!is_error(ret) && arg2
                && put_user_ual(deathsig, arg2)) {
                return -TARGET_EFAULT;
            }
            return ret;
        }
#ifdef PR_GET_NAME
        case PR_GET_NAME:
        {
            void *name = lock_user(VERIFY_WRITE, arg2, 16, 1);
            if (!name) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(prctl(arg1, (unsigned long)name,
                                  arg3, arg4, arg5));
            unlock_user(name, arg2, 16);
            return ret;
        }
        case PR_SET_NAME:
        {
            void *name = lock_user(VERIFY_READ, arg2, 16, 1);
            if (!name) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(prctl(arg1, (unsigned long)name,
                                  arg3, arg4, arg5));
            unlock_user(name, arg2, 0);
            return ret;
        }
#endif
#ifdef TARGET_MIPS
        case TARGET_PR_GET_FP_MODE:
        {
            CPUMIPSState *env = ((CPUMIPSState *)cpu_env);
            ret = 0;
            if (env->CP0_Status & (1 << CP0St_FR)) {
                ret |= TARGET_PR_FP_MODE_FR;
            }
            if (env->CP0_Config5 & (1 << CP0C5_FRE)) {
                ret |= TARGET_PR_FP_MODE_FRE;
            }
            return ret;
        }
        case TARGET_PR_SET_FP_MODE:
        {
            CPUMIPSState *env = ((CPUMIPSState *)cpu_env);
            bool old_fr = env->CP0_Status & (1 << CP0St_FR);
            bool old_fre = env->CP0_Config5 & (1 << CP0C5_FRE);
            bool new_fr = arg2 & TARGET_PR_FP_MODE_FR;
            bool new_fre = arg2 & TARGET_PR_FP_MODE_FRE;

            const unsigned int known_bits = TARGET_PR_FP_MODE_FR |
                                            TARGET_PR_FP_MODE_FRE;

            /* If nothing to change, return right away, successfully.  */
            if (old_fr == new_fr && old_fre == new_fre) {
                return 0;
            }
            /* Check the value is valid */
            if (arg2 & ~known_bits) {
                return -TARGET_EOPNOTSUPP;
            }
            /* Setting FRE without FR is not supported.  */
            if (new_fre && !new_fr) {
                return -TARGET_EOPNOTSUPP;
            }
            if (new_fr && !(env->active_fpu.fcr0 & (1 << FCR0_F64))) {
                /* FR1 is not supported */
                return -TARGET_EOPNOTSUPP;
            }
            if (!new_fr && (env->active_fpu.fcr0 & (1 << FCR0_F64))
                && !(env->CP0_Status_rw_bitmask & (1 << CP0St_FR))) {
                /* cannot set FR=0 */
                return -TARGET_EOPNOTSUPP;
            }
            if (new_fre && !(env->active_fpu.fcr0 & (1 << FCR0_FREP))) {
                /* Cannot set FRE=1 */
                return -TARGET_EOPNOTSUPP;
            }

            int i;
            fpr_t *fpr = env->active_fpu.fpr;
            for (i = 0; i < 32 ; i += 2) {
                if (!old_fr && new_fr) {
                    fpr[i].w[!FP_ENDIAN_IDX] = fpr[i + 1].w[FP_ENDIAN_IDX];
                } else if (old_fr && !new_fr) {
                    fpr[i + 1].w[FP_ENDIAN_IDX] = fpr[i].w[!FP_ENDIAN_IDX];
                }
            }

            if (new_fr) {
                env->CP0_Status |= (1 << CP0St_FR);
                env->hflags |= MIPS_HFLAG_F64;
            } else {
                env->CP0_Status &= ~(1 << CP0St_FR);
                env->hflags &= ~MIPS_HFLAG_F64;
            }
            if (new_fre) {
                env->CP0_Config5 |= (1 << CP0C5_FRE);
                if (env->active_fpu.fcr0 & (1 << FCR0_FREP)) {
                    env->hflags |= MIPS_HFLAG_FRE;
                }
            } else {
                env->CP0_Config5 &= ~(1 << CP0C5_FRE);
                env->hflags &= ~MIPS_HFLAG_FRE;
            }

            return 0;
        }
#endif /* MIPS */
#ifdef TARGET_AARCH64
        case TARGET_PR_SVE_SET_VL:
            /*
             * We cannot support either PR_SVE_SET_VL_ONEXEC or
             * PR_SVE_VL_INHERIT.  Note the kernel definition
             * of sve_vl_valid allows for VQ=512, i.e. VL=8192,
             * even though the current architectural maximum is VQ=16.
             */
            ret = -TARGET_EINVAL;
            if (cpu_isar_feature(aa64_sve, arm_env_get_cpu(cpu_env))
                && arg2 >= 0 && arg2 <= 512 * 16 && !(arg2 & 15)) {
                CPUARMState *env = cpu_env;
                ARMCPU *cpu = arm_env_get_cpu(env);
                uint32_t vq, old_vq;

                old_vq = (env->vfp.zcr_el[1] & 0xf) + 1;
                vq = MAX(arg2 / 16, 1);
                vq = MIN(vq, cpu->sve_max_vq);

                if (vq < old_vq) {
                    aarch64_sve_narrow_vq(env, vq);
                }
                env->vfp.zcr_el[1] = vq - 1;
                ret = vq * 16;
            }
            return ret;
        case TARGET_PR_SVE_GET_VL:
            ret = -TARGET_EINVAL;
            {
                ARMCPU *cpu = arm_env_get_cpu(cpu_env);
                if (cpu_isar_feature(aa64_sve, cpu)) {
                    ret = ((cpu->env.vfp.zcr_el[1] & 0xf) + 1) * 16;
                }
            }
            return ret;
        case TARGET_PR_PAC_RESET_KEYS:
            {
                CPUARMState *env = cpu_env;
                ARMCPU *cpu = arm_env_get_cpu(env);

                if (arg3 || arg4 || arg5) {
                    return -TARGET_EINVAL;
                }
                if (cpu_isar_feature(aa64_pauth, cpu)) {
                    int all = (TARGET_PR_PAC_APIAKEY | TARGET_PR_PAC_APIBKEY |
                               TARGET_PR_PAC_APDAKEY | TARGET_PR_PAC_APDBKEY |
                               TARGET_PR_PAC_APGAKEY);
                    if (arg2 == 0) {
                        arg2 = all;
                    } else if (arg2 & ~all) {
                        return -TARGET_EINVAL;
                    }
                    if (arg2 & TARGET_PR_PAC_APIAKEY) {
                        arm_init_pauth_key(&env->apia_key);
                    }
                    if (arg2 & TARGET_PR_PAC_APIBKEY) {
                        arm_init_pauth_key(&env->apib_key);
                    }
                    if (arg2 & TARGET_PR_PAC_APDAKEY) {
                        arm_init_pauth_key(&env->apda_key);
                    }
                    if (arg2 & TARGET_PR_PAC_APDBKEY) {
                        arm_init_pauth_key(&env->apdb_key);
                    }
                    if (arg2 & TARGET_PR_PAC_APGAKEY) {
                        arm_init_pauth_key(&env->apga_key);
                    }
                    return 0;
                }
            }
            return -TARGET_EINVAL;
#endif /* AARCH64 */
        case PR_GET_SECCOMP:
        case PR_SET_SECCOMP:
            /* Disable seccomp to prevent the target disabling syscalls we
             * need. */
            return -TARGET_EINVAL;
        default:
            /* Most prctl options have no pointer arguments */
            return get_errno(prctl(arg1, arg2, arg3, arg4, arg5));
        }
        break;
#ifdef TARGET_NR_arch_prctl
    case TARGET_NR_arch_prctl:
#if defined(TARGET_I386) && !defined(TARGET_ABI32)
        return do_arch_prctl(cpu_env, arg1, arg2);
#else
#error unreachable
#endif
#endif
    case TARGET_NR_getcwd:
        if (!(p = lock_user(VERIFY_WRITE, arg1, arg2, 0)))
            return -TARGET_EFAULT;
        ret = get_errno(sys_getcwd1(p, arg2));
        unlock_user(p, arg1, ret);
        return ret;
    case TARGET_NR_capget:
    case TARGET_NR_capset:
    {
        struct target_user_cap_header *target_header;
        struct target_user_cap_data *target_data = NULL;
        struct __user_cap_header_struct header;
        struct __user_cap_data_struct data[2];
        struct __user_cap_data_struct *dataptr = NULL;
        int i, target_datalen;
        int data_items = 1;

        if (!lock_user_struct(VERIFY_WRITE, target_header, arg1, 1)) {
            return -TARGET_EFAULT;
        }
        header.version = tswap32(target_header->version);
        header.pid = tswap32(target_header->pid);

        if (header.version != _LINUX_CAPABILITY_VERSION) {
            /* Version 2 and up takes pointer to two user_data structs */
            data_items = 2;
        }

        target_datalen = sizeof(*target_data) * data_items;

        if (arg2) {
            if (num == TARGET_NR_capget) {
                target_data = lock_user(VERIFY_WRITE, arg2, target_datalen, 0);
            } else {
                target_data = lock_user(VERIFY_READ, arg2, target_datalen, 1);
            }
            if (!target_data) {
                unlock_user_struct(target_header, arg1, 0);
                return -TARGET_EFAULT;
            }

            if (num == TARGET_NR_capset) {
                for (i = 0; i < data_items; i++) {
                    data[i].effective = tswap32(target_data[i].effective);
                    data[i].permitted = tswap32(target_data[i].permitted);
                    data[i].inheritable = tswap32(target_data[i].inheritable);
                }
            }

            dataptr = data;
        }

        if (num == TARGET_NR_capget) {
            ret = get_errno(capget(&header, dataptr));
        } else {
            ret = get_errno(capset(&header, dataptr));
        }

        /* The kernel always updates version for both capget and capset */
        target_header->version = tswap32(header.version);
        unlock_user_struct(target_header, arg1, 1);

        if (arg2) {
            if (num == TARGET_NR_capget) {
                for (i = 0; i < data_items; i++) {
                    target_data[i].effective = tswap32(data[i].effective);
                    target_data[i].permitted = tswap32(data[i].permitted);
                    target_data[i].inheritable = tswap32(data[i].inheritable);
                }
                unlock_user(target_data, arg2, target_datalen);
            } else {
                unlock_user(target_data, arg2, 0);
            }
        }
        return ret;
    }
    case TARGET_NR_sigaltstack:
        return do_sigaltstack(arg1, arg2,
                              get_sp_from_cpustate((CPUArchState *)cpu_env));

#ifdef CONFIG_SENDFILE
#ifdef TARGET_NR_sendfile
    case TARGET_NR_sendfile:
    {
        off_t *offp = NULL;
        off_t off;
        if (arg3) {
            ret = get_user_sal(off, arg3);
            if (is_error(ret)) {
                return ret;
            }
            offp = &off;
        }
        ret = get_errno(sendfile(arg1, arg2, offp, arg4));
        if (!is_error(ret) && arg3) {
            abi_long ret2 = put_user_sal(off, arg3);
            if (is_error(ret2)) {
                ret = ret2;
            }
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_sendfile64
    case TARGET_NR_sendfile64:
    {
        off_t *offp = NULL;
        off_t off;
        if (arg3) {
            ret = get_user_s64(off, arg3);
            if (is_error(ret)) {
                return ret;
            }
            offp = &off;
        }
        ret = get_errno(sendfile(arg1, arg2, offp, arg4));
        if (!is_error(ret) && arg3) {
            abi_long ret2 = put_user_s64(off, arg3);
            if (is_error(ret2)) {
                ret = ret2;
            }
        }
        return ret;
    }
#endif
#endif
#ifdef TARGET_NR_ugetrlimit
    case TARGET_NR_ugetrlimit:
    {
	struct rlimit rlim;
	int resource = target_to_host_resource(arg1);
	ret = get_errno(getrlimit(resource, &rlim));
	if (!is_error(ret)) {
	    struct target_rlimit *target_rlim;
            if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0))
                return -TARGET_EFAULT;
	    target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
	    target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
            unlock_user_struct(target_rlim, arg2, 1);
	}
        return ret;
    }
#endif
#ifdef TARGET_NR_truncate64
    case TARGET_NR_truncate64:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
	ret = target_truncate64(cpu_env, p, arg2, arg3, arg4);
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_ftruncate64
    case TARGET_NR_ftruncate64:
        return target_ftruncate64(cpu_env, arg1, arg2, arg3, arg4);
#endif
#ifdef TARGET_NR_stat64
    case TARGET_NR_stat64:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(stat(path(p), &st));
        unlock_user(p, arg1, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        return ret;
#endif
#ifdef TARGET_NR_lstat64
    case TARGET_NR_lstat64:
        if (!(p = lock_user_string(arg1))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(lstat(path(p), &st));
        unlock_user(p, arg1, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        return ret;
#endif
#ifdef TARGET_NR_fstat64
    case TARGET_NR_fstat64:
        ret = get_errno(fstat(arg1, &st));
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg2, &st);
        return ret;
#endif
#if (defined(TARGET_NR_fstatat64) || defined(TARGET_NR_newfstatat))
#ifdef TARGET_NR_fstatat64
    case TARGET_NR_fstatat64:
#endif
#ifdef TARGET_NR_newfstatat
    case TARGET_NR_newfstatat:
#endif
        if (!(p = lock_user_string(arg2))) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(fstatat(arg1, path(p), &st, arg4));
        unlock_user(p, arg2, 0);
        if (!is_error(ret))
            ret = host_to_target_stat64(cpu_env, arg3, &st);
        return ret;
#endif
#ifdef TARGET_NR_lchown
    case TARGET_NR_lchown:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(lchown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_getuid
    case TARGET_NR_getuid:
        return get_errno(high2lowuid(getuid()));
#endif
#ifdef TARGET_NR_getgid
    case TARGET_NR_getgid:
        return get_errno(high2lowgid(getgid()));
#endif
#ifdef TARGET_NR_geteuid
    case TARGET_NR_geteuid:
        return get_errno(high2lowuid(geteuid()));
#endif
#ifdef TARGET_NR_getegid
    case TARGET_NR_getegid:
        return get_errno(high2lowgid(getegid()));
#endif
    case TARGET_NR_setreuid:
        return get_errno(setreuid(low2highuid(arg1), low2highuid(arg2)));
    case TARGET_NR_setregid:
        return get_errno(setregid(low2highgid(arg1), low2highgid(arg2)));
    case TARGET_NR_getgroups:
        {
            int gidsetsize = arg1;
            target_id *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (gidsetsize == 0)
                return ret;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * sizeof(target_id), 0);
                if (!target_grouplist)
                    return -TARGET_EFAULT;
                for(i = 0;i < ret; i++)
                    target_grouplist[i] = tswapid(high2lowgid(grouplist[i]));
                unlock_user(target_grouplist, arg2, gidsetsize * sizeof(target_id));
            }
        }
        return ret;
    case TARGET_NR_setgroups:
        {
            int gidsetsize = arg1;
            target_id *target_grouplist;
            gid_t *grouplist = NULL;
            int i;
            if (gidsetsize) {
                grouplist = alloca(gidsetsize * sizeof(gid_t));
                target_grouplist = lock_user(VERIFY_READ, arg2, gidsetsize * sizeof(target_id), 1);
                if (!target_grouplist) {
                    return -TARGET_EFAULT;
                }
                for (i = 0; i < gidsetsize; i++) {
                    grouplist[i] = low2highgid(tswapid(target_grouplist[i]));
                }
                unlock_user(target_grouplist, arg2, 0);
            }
            return get_errno(setgroups(gidsetsize, grouplist));
        }
    case TARGET_NR_fchown:
        return get_errno(fchown(arg1, low2highuid(arg2), low2highgid(arg3)));
#if defined(TARGET_NR_fchownat)
    case TARGET_NR_fchownat:
        if (!(p = lock_user_string(arg2))) 
            return -TARGET_EFAULT;
        ret = get_errno(fchownat(arg1, p, low2highuid(arg3),
                                 low2highgid(arg4), arg5));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#ifdef TARGET_NR_setresuid
    case TARGET_NR_setresuid:
        return get_errno(sys_setresuid(low2highuid(arg1),
                                       low2highuid(arg2),
                                       low2highuid(arg3)));
#endif
#ifdef TARGET_NR_getresuid
    case TARGET_NR_getresuid:
        {
            uid_t ruid, euid, suid;
            ret = get_errno(getresuid(&ruid, &euid, &suid));
            if (!is_error(ret)) {
                if (put_user_id(high2lowuid(ruid), arg1)
                    || put_user_id(high2lowuid(euid), arg2)
                    || put_user_id(high2lowuid(suid), arg3))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_getresgid
    case TARGET_NR_setresgid:
        return get_errno(sys_setresgid(low2highgid(arg1),
                                       low2highgid(arg2),
                                       low2highgid(arg3)));
#endif
#ifdef TARGET_NR_getresgid
    case TARGET_NR_getresgid:
        {
            gid_t rgid, egid, sgid;
            ret = get_errno(getresgid(&rgid, &egid, &sgid));
            if (!is_error(ret)) {
                if (put_user_id(high2lowgid(rgid), arg1)
                    || put_user_id(high2lowgid(egid), arg2)
                    || put_user_id(high2lowgid(sgid), arg3))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_chown
    case TARGET_NR_chown:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chown(p, low2highuid(arg2), low2highgid(arg3)));
        unlock_user(p, arg1, 0);
        return ret;
#endif
    case TARGET_NR_setuid:
        return get_errno(sys_setuid(low2highuid(arg1)));
    case TARGET_NR_setgid:
        return get_errno(sys_setgid(low2highgid(arg1)));
    case TARGET_NR_setfsuid:
        return get_errno(setfsuid(arg1));
    case TARGET_NR_setfsgid:
        return get_errno(setfsgid(arg1));

#ifdef TARGET_NR_lchown32
    case TARGET_NR_lchown32:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(lchown(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_getuid32
    case TARGET_NR_getuid32:
        return get_errno(getuid());
#endif

#if defined(TARGET_NR_getxuid) && defined(TARGET_ALPHA)
   /* Alpha specific */
    case TARGET_NR_getxuid:
         {
            uid_t euid;
            euid=geteuid();
            ((CPUAlphaState *)cpu_env)->ir[IR_A4]=euid;
         }
        return get_errno(getuid());
#endif
#if defined(TARGET_NR_getxgid) && defined(TARGET_ALPHA)
   /* Alpha specific */
    case TARGET_NR_getxgid:
         {
            uid_t egid;
            egid=getegid();
            ((CPUAlphaState *)cpu_env)->ir[IR_A4]=egid;
         }
        return get_errno(getgid());
#endif
#if defined(TARGET_NR_osf_getsysinfo) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_osf_getsysinfo:
        ret = -TARGET_EOPNOTSUPP;
        switch (arg1) {
          case TARGET_GSI_IEEE_FP_CONTROL:
            {
                uint64_t swcr, fpcr = cpu_alpha_load_fpcr (cpu_env);

                /* Copied from linux ieee_fpcr_to_swcr.  */
                swcr = (fpcr >> 35) & SWCR_STATUS_MASK;
                swcr |= (fpcr >> 36) & SWCR_MAP_DMZ;
                swcr |= (~fpcr >> 48) & (SWCR_TRAP_ENABLE_INV
                                        | SWCR_TRAP_ENABLE_DZE
                                        | SWCR_TRAP_ENABLE_OVF);
                swcr |= (~fpcr >> 57) & (SWCR_TRAP_ENABLE_UNF
                                        | SWCR_TRAP_ENABLE_INE);
                swcr |= (fpcr >> 47) & SWCR_MAP_UMZ;
                swcr |= (~fpcr >> 41) & SWCR_TRAP_ENABLE_DNO;

                if (put_user_u64 (swcr, arg2))
                        return -TARGET_EFAULT;
                ret = 0;
            }
            break;

          /* case GSI_IEEE_STATE_AT_SIGNAL:
             -- Not implemented in linux kernel.
             case GSI_UACPROC:
             -- Retrieves current unaligned access state; not much used.
             case GSI_PROC_TYPE:
             -- Retrieves implver information; surely not used.
             case GSI_GET_HWRPB:
             -- Grabs a copy of the HWRPB; surely not used.
          */
        }
        return ret;
#endif
#if defined(TARGET_NR_osf_setsysinfo) && defined(TARGET_ALPHA)
    /* Alpha specific */
    case TARGET_NR_osf_setsysinfo:
        ret = -TARGET_EOPNOTSUPP;
        switch (arg1) {
          case TARGET_SSI_IEEE_FP_CONTROL:
            {
                uint64_t swcr, fpcr, orig_fpcr;

                if (get_user_u64 (swcr, arg2)) {
                    return -TARGET_EFAULT;
                }
                orig_fpcr = cpu_alpha_load_fpcr(cpu_env);
                fpcr = orig_fpcr & FPCR_DYN_MASK;

                /* Copied from linux ieee_swcr_to_fpcr.  */
                fpcr |= (swcr & SWCR_STATUS_MASK) << 35;
                fpcr |= (swcr & SWCR_MAP_DMZ) << 36;
                fpcr |= (~swcr & (SWCR_TRAP_ENABLE_INV
                                  | SWCR_TRAP_ENABLE_DZE
                                  | SWCR_TRAP_ENABLE_OVF)) << 48;
                fpcr |= (~swcr & (SWCR_TRAP_ENABLE_UNF
                                  | SWCR_TRAP_ENABLE_INE)) << 57;
                fpcr |= (swcr & SWCR_MAP_UMZ ? FPCR_UNDZ | FPCR_UNFD : 0);
                fpcr |= (~swcr & SWCR_TRAP_ENABLE_DNO) << 41;

                cpu_alpha_store_fpcr(cpu_env, fpcr);
                ret = 0;
            }
            break;

          case TARGET_SSI_IEEE_RAISE_EXCEPTION:
            {
                uint64_t exc, fpcr, orig_fpcr;
                int si_code;

                if (get_user_u64(exc, arg2)) {
                    return -TARGET_EFAULT;
                }

                orig_fpcr = cpu_alpha_load_fpcr(cpu_env);

                /* We only add to the exception status here.  */
                fpcr = orig_fpcr | ((exc & SWCR_STATUS_MASK) << 35);

                cpu_alpha_store_fpcr(cpu_env, fpcr);
                ret = 0;

                /* Old exceptions are not signaled.  */
                fpcr &= ~(orig_fpcr & FPCR_STATUS_MASK);

                /* If any exceptions set by this call,
                   and are unmasked, send a signal.  */
                si_code = 0;
                if ((fpcr & (FPCR_INE | FPCR_INED)) == FPCR_INE) {
                    si_code = TARGET_FPE_FLTRES;
                }
                if ((fpcr & (FPCR_UNF | FPCR_UNFD)) == FPCR_UNF) {
                    si_code = TARGET_FPE_FLTUND;
                }
                if ((fpcr & (FPCR_OVF | FPCR_OVFD)) == FPCR_OVF) {
                    si_code = TARGET_FPE_FLTOVF;
                }
                if ((fpcr & (FPCR_DZE | FPCR_DZED)) == FPCR_DZE) {
                    si_code = TARGET_FPE_FLTDIV;
                }
                if ((fpcr & (FPCR_INV | FPCR_INVD)) == FPCR_INV) {
                    si_code = TARGET_FPE_FLTINV;
                }
                if (si_code != 0) {
                    target_siginfo_t info;
                    info.si_signo = SIGFPE;
                    info.si_errno = 0;
                    info.si_code = si_code;
                    info._sifields._sigfault._addr
                        = ((CPUArchState *)cpu_env)->pc;
                    queue_signal((CPUArchState *)cpu_env, info.si_signo,
                                 QEMU_SI_FAULT, &info);
                }
            }
            break;

          /* case SSI_NVPAIRS:
             -- Used with SSIN_UACPROC to enable unaligned accesses.
             case SSI_IEEE_STATE_AT_SIGNAL:
             case SSI_IEEE_IGNORE_STATE_AT_SIGNAL:
             -- Not implemented in linux kernel
          */
        }
        return ret;
#endif
#ifdef TARGET_NR_osf_sigprocmask
    /* Alpha specific.  */
    case TARGET_NR_osf_sigprocmask:
        {
            abi_ulong mask;
            int how;
            sigset_t set, oldset;

            switch(arg1) {
            case TARGET_SIG_BLOCK:
                how = SIG_BLOCK;
                break;
            case TARGET_SIG_UNBLOCK:
                how = SIG_UNBLOCK;
                break;
            case TARGET_SIG_SETMASK:
                how = SIG_SETMASK;
                break;
            default:
                return -TARGET_EINVAL;
            }
            mask = arg2;
            target_to_host_old_sigset(&set, &mask);
            ret = do_sigprocmask(how, &set, &oldset);
            if (!ret) {
                host_to_target_old_sigset(&mask, &oldset);
                ret = mask;
            }
        }
        return ret;
#endif

#ifdef TARGET_NR_getgid32
    case TARGET_NR_getgid32:
        return get_errno(getgid());
#endif
#ifdef TARGET_NR_geteuid32
    case TARGET_NR_geteuid32:
        return get_errno(geteuid());
#endif
#ifdef TARGET_NR_getegid32
    case TARGET_NR_getegid32:
        return get_errno(getegid());
#endif
#ifdef TARGET_NR_setreuid32
    case TARGET_NR_setreuid32:
        return get_errno(setreuid(arg1, arg2));
#endif
#ifdef TARGET_NR_setregid32
    case TARGET_NR_setregid32:
        return get_errno(setregid(arg1, arg2));
#endif
#ifdef TARGET_NR_getgroups32
    case TARGET_NR_getgroups32:
        {
            int gidsetsize = arg1;
            uint32_t *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            ret = get_errno(getgroups(gidsetsize, grouplist));
            if (gidsetsize == 0)
                return ret;
            if (!is_error(ret)) {
                target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * 4, 0);
                if (!target_grouplist) {
                    return -TARGET_EFAULT;
                }
                for(i = 0;i < ret; i++)
                    target_grouplist[i] = tswap32(grouplist[i]);
                unlock_user(target_grouplist, arg2, gidsetsize * 4);
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_setgroups32
    case TARGET_NR_setgroups32:
        {
            int gidsetsize = arg1;
            uint32_t *target_grouplist;
            gid_t *grouplist;
            int i;

            grouplist = alloca(gidsetsize * sizeof(gid_t));
            target_grouplist = lock_user(VERIFY_READ, arg2, gidsetsize * 4, 1);
            if (!target_grouplist) {
                return -TARGET_EFAULT;
            }
            for(i = 0;i < gidsetsize; i++)
                grouplist[i] = tswap32(target_grouplist[i]);
            unlock_user(target_grouplist, arg2, 0);
            return get_errno(setgroups(gidsetsize, grouplist));
        }
#endif
#ifdef TARGET_NR_fchown32
    case TARGET_NR_fchown32:
        return get_errno(fchown(arg1, arg2, arg3));
#endif
#ifdef TARGET_NR_setresuid32
    case TARGET_NR_setresuid32:
        return get_errno(sys_setresuid(arg1, arg2, arg3));
#endif
#ifdef TARGET_NR_getresuid32
    case TARGET_NR_getresuid32:
        {
            uid_t ruid, euid, suid;
            ret = get_errno(getresuid(&ruid, &euid, &suid));
            if (!is_error(ret)) {
                if (put_user_u32(ruid, arg1)
                    || put_user_u32(euid, arg2)
                    || put_user_u32(suid, arg3))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_setresgid32
    case TARGET_NR_setresgid32:
        return get_errno(sys_setresgid(arg1, arg2, arg3));
#endif
#ifdef TARGET_NR_getresgid32
    case TARGET_NR_getresgid32:
        {
            gid_t rgid, egid, sgid;
            ret = get_errno(getresgid(&rgid, &egid, &sgid));
            if (!is_error(ret)) {
                if (put_user_u32(rgid, arg1)
                    || put_user_u32(egid, arg2)
                    || put_user_u32(sgid, arg3))
                    return -TARGET_EFAULT;
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_chown32
    case TARGET_NR_chown32:
        if (!(p = lock_user_string(arg1)))
            return -TARGET_EFAULT;
        ret = get_errno(chown(p, arg2, arg3));
        unlock_user(p, arg1, 0);
        return ret;
#endif
#ifdef TARGET_NR_setuid32
    case TARGET_NR_setuid32:
        return get_errno(sys_setuid(arg1));
#endif
#ifdef TARGET_NR_setgid32
    case TARGET_NR_setgid32:
        return get_errno(sys_setgid(arg1));
#endif
#ifdef TARGET_NR_setfsuid32
    case TARGET_NR_setfsuid32:
        return get_errno(setfsuid(arg1));
#endif
#ifdef TARGET_NR_setfsgid32
    case TARGET_NR_setfsgid32:
        return get_errno(setfsgid(arg1));
#endif
#ifdef TARGET_NR_mincore
    case TARGET_NR_mincore:
        {
            void *a = lock_user(VERIFY_READ, arg1, arg2, 0);
            if (!a) {
                return -TARGET_ENOMEM;
            }
            p = lock_user_string(arg3);
            if (!p) {
                ret = -TARGET_EFAULT;
            } else {
                ret = get_errno(mincore(a, arg2, p));
                unlock_user(p, arg3, ret);
            }
            unlock_user(a, arg1, 0);
        }
        return ret;
#endif
#ifdef TARGET_NR_arm_fadvise64_64
    case TARGET_NR_arm_fadvise64_64:
        /* arm_fadvise64_64 looks like fadvise64_64 but
         * with different argument order: fd, advice, offset, len
         * rather than the usual fd, offset, len, advice.
         * Note that offset and len are both 64-bit so appear as
         * pairs of 32-bit registers.
         */
        ret = posix_fadvise(arg1, target_offset64(arg3, arg4),
                            target_offset64(arg5, arg6), arg2);
        return -host_to_target_errno(ret);
#endif

#if TARGET_ABI_BITS == 32

#ifdef TARGET_NR_fadvise64_64
    case TARGET_NR_fadvise64_64:
#if defined(TARGET_PPC) || defined(TARGET_XTENSA)
        /* 6 args: fd, advice, offset (high, low), len (high, low) */
        ret = arg2;
        arg2 = arg3;
        arg3 = arg4;
        arg4 = arg5;
        arg5 = arg6;
        arg6 = ret;
#else
        /* 6 args: fd, offset (high, low), len (high, low), advice */
        if (regpairs_aligned(cpu_env, num)) {
            /* offset is in (3,4), len in (5,6) and advice in 7 */
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
            arg5 = arg6;
            arg6 = arg7;
        }
#endif
        ret = posix_fadvise(arg1, target_offset64(arg2, arg3),
                            target_offset64(arg4, arg5), arg6);
        return -host_to_target_errno(ret);
#endif

#ifdef TARGET_NR_fadvise64
    case TARGET_NR_fadvise64:
        /* 5 args: fd, offset (high, low), len, advice */
        if (regpairs_aligned(cpu_env, num)) {
            /* offset is in (3,4), len in 5 and advice in 6 */
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
            arg5 = arg6;
        }
        ret = posix_fadvise(arg1, target_offset64(arg2, arg3), arg4, arg5);
        return -host_to_target_errno(ret);
#endif

#else /* not a 32-bit ABI */
#if defined(TARGET_NR_fadvise64_64) || defined(TARGET_NR_fadvise64)
#ifdef TARGET_NR_fadvise64_64
    case TARGET_NR_fadvise64_64:
#endif
#ifdef TARGET_NR_fadvise64
    case TARGET_NR_fadvise64:
#endif
#ifdef TARGET_S390X
        switch (arg4) {
        case 4: arg4 = POSIX_FADV_NOREUSE + 1; break; /* make sure it's an invalid value */
        case 5: arg4 = POSIX_FADV_NOREUSE + 2; break; /* ditto */
        case 6: arg4 = POSIX_FADV_DONTNEED; break;
        case 7: arg4 = POSIX_FADV_NOREUSE; break;
        default: break;
        }
#endif
        return -host_to_target_errno(posix_fadvise(arg1, arg2, arg3, arg4));
#endif
#endif /* end of 64-bit ABI fadvise handling */

#ifdef TARGET_NR_madvise
    case TARGET_NR_madvise:
        /* A straight passthrough may not be safe because qemu sometimes
           turns private file-backed mappings into anonymous mappings.
           This will break MADV_DONTNEED.
           This is a hint, so ignoring and returning success is ok.  */
        return 0;
#endif
#ifdef TARGET_NR_cacheflush
    case TARGET_NR_cacheflush:
        /* self-modifying code is handled automatically, so nothing needed */
        return 0;
#endif
#ifdef TARGET_NR_getpagesize
    case TARGET_NR_getpagesize:
        return TARGET_PAGE_SIZE;
#endif
    case TARGET_NR_gettid:
        return get_errno(sys_gettid());
#ifdef TARGET_NR_readahead
    case TARGET_NR_readahead:
#if TARGET_ABI_BITS == 32
        if (regpairs_aligned(cpu_env, num)) {
            arg2 = arg3;
            arg3 = arg4;
            arg4 = arg5;
        }
        ret = get_errno(readahead(arg1, target_offset64(arg2, arg3) , arg4));
#else
        ret = get_errno(readahead(arg1, arg2, arg3));
#endif
        return ret;
#endif
#ifdef CONFIG_ATTR
#ifdef TARGET_NR_setxattr
    case TARGET_NR_listxattr:
    case TARGET_NR_llistxattr:
    {
        void *p, *b = 0;
        if (arg2) {
            b = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!b) {
                return -TARGET_EFAULT;
            }
        }
        p = lock_user_string(arg1);
        if (p) {
            if (num == TARGET_NR_listxattr) {
                ret = get_errno(listxattr(p, b, arg3));
            } else {
                ret = get_errno(llistxattr(p, b, arg3));
            }
        } else {
            ret = -TARGET_EFAULT;
        }
        unlock_user(p, arg1, 0);
        unlock_user(b, arg2, arg3);
        return ret;
    }
    case TARGET_NR_flistxattr:
    {
        void *b = 0;
        if (arg2) {
            b = lock_user(VERIFY_WRITE, arg2, arg3, 0);
            if (!b) {
                return -TARGET_EFAULT;
            }
        }
        ret = get_errno(flistxattr(arg1, b, arg3));
        unlock_user(b, arg2, arg3);
        return ret;
    }
    case TARGET_NR_setxattr:
    case TARGET_NR_lsetxattr:
        {
            void *p, *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_READ, arg3, arg4, 1);
                if (!v) {
                    return -TARGET_EFAULT;
                }
            }
            p = lock_user_string(arg1);
            n = lock_user_string(arg2);
            if (p && n) {
                if (num == TARGET_NR_setxattr) {
                    ret = get_errno(setxattr(p, n, v, arg4, arg5));
                } else {
                    ret = get_errno(lsetxattr(p, n, v, arg4, arg5));
                }
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(p, arg1, 0);
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, 0);
        }
        return ret;
    case TARGET_NR_fsetxattr:
        {
            void *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_READ, arg3, arg4, 1);
                if (!v) {
                    return -TARGET_EFAULT;
                }
            }
            n = lock_user_string(arg2);
            if (n) {
                ret = get_errno(fsetxattr(arg1, n, v, arg4, arg5));
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, 0);
        }
        return ret;
    case TARGET_NR_getxattr:
    case TARGET_NR_lgetxattr:
        {
            void *p, *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!v) {
                    return -TARGET_EFAULT;
                }
            }
            p = lock_user_string(arg1);
            n = lock_user_string(arg2);
            if (p && n) {
                if (num == TARGET_NR_getxattr) {
                    ret = get_errno(getxattr(p, n, v, arg4));
                } else {
                    ret = get_errno(lgetxattr(p, n, v, arg4));
                }
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(p, arg1, 0);
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, arg4);
        }
        return ret;
    case TARGET_NR_fgetxattr:
        {
            void *n, *v = 0;
            if (arg3) {
                v = lock_user(VERIFY_WRITE, arg3, arg4, 0);
                if (!v) {
                    return -TARGET_EFAULT;
                }
            }
            n = lock_user_string(arg2);
            if (n) {
                ret = get_errno(fgetxattr(arg1, n, v, arg4));
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(n, arg2, 0);
            unlock_user(v, arg3, arg4);
        }
        return ret;
    case TARGET_NR_removexattr:
    case TARGET_NR_lremovexattr:
        {
            void *p, *n;
            p = lock_user_string(arg1);
            n = lock_user_string(arg2);
            if (p && n) {
                if (num == TARGET_NR_removexattr) {
                    ret = get_errno(removexattr(p, n));
                } else {
                    ret = get_errno(lremovexattr(p, n));
                }
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(p, arg1, 0);
            unlock_user(n, arg2, 0);
        }
        return ret;
    case TARGET_NR_fremovexattr:
        {
            void *n;
            n = lock_user_string(arg2);
            if (n) {
                ret = get_errno(fremovexattr(arg1, n));
            } else {
                ret = -TARGET_EFAULT;
            }
            unlock_user(n, arg2, 0);
        }
        return ret;
#endif
#endif /* CONFIG_ATTR */
#ifdef TARGET_NR_set_thread_area
    case TARGET_NR_set_thread_area:
#if defined(TARGET_MIPS)
      ((CPUMIPSState *) cpu_env)->active_tc.CP0_UserLocal = arg1;
      return 0;
#elif defined(TARGET_CRIS)
      if (arg1 & 0xff)
          ret = -TARGET_EINVAL;
      else {
          ((CPUCRISState *) cpu_env)->pregs[PR_PID] = arg1;
          ret = 0;
      }
      return ret;
#elif defined(TARGET_I386) && defined(TARGET_ABI32)
      return do_set_thread_area(cpu_env, arg1);
#elif defined(TARGET_M68K)
      {
          TaskState *ts = cpu->opaque;
          ts->tp_value = arg1;
          return 0;
      }
#else
      return -TARGET_ENOSYS;
#endif
#endif
#ifdef TARGET_NR_get_thread_area
    case TARGET_NR_get_thread_area:
#if defined(TARGET_I386) && defined(TARGET_ABI32)
        return do_get_thread_area(cpu_env, arg1);
#elif defined(TARGET_M68K)
        {
            TaskState *ts = cpu->opaque;
            return ts->tp_value;
        }
#else
        return -TARGET_ENOSYS;
#endif
#endif
#ifdef TARGET_NR_getdomainname
    case TARGET_NR_getdomainname:
        return -TARGET_ENOSYS;
#endif

#ifdef TARGET_NR_clock_settime
    case TARGET_NR_clock_settime:
    {
        struct timespec ts;

        ret = target_to_host_timespec(&ts, arg2);
        if (!is_error(ret)) {
            ret = get_errno(clock_settime(arg1, &ts));
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_gettime
    case TARGET_NR_clock_gettime:
    {
        struct timespec ts;
        ret = get_errno(clock_gettime(arg1, &ts));
        if (!is_error(ret)) {
            ret = host_to_target_timespec(arg2, &ts);
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_getres
    case TARGET_NR_clock_getres:
    {
        struct timespec ts;
        ret = get_errno(clock_getres(arg1, &ts));
        if (!is_error(ret)) {
            host_to_target_timespec(arg2, &ts);
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_clock_nanosleep
    case TARGET_NR_clock_nanosleep:
    {
        struct timespec ts;
        target_to_host_timespec(&ts, arg3);
        ret = get_errno(safe_clock_nanosleep(arg1, arg2,
                                             &ts, arg4 ? &ts : NULL));
        if (arg4)
            host_to_target_timespec(arg4, &ts);

#if defined(TARGET_PPC)
        /* clock_nanosleep is odd in that it returns positive errno values.
         * On PPC, CR0 bit 3 should be set in such a situation. */
        if (ret && ret != -TARGET_ERESTARTSYS) {
            ((CPUPPCState *)cpu_env)->crf[0] |= 1;
        }
#endif
        return ret;
    }
#endif

#if defined(TARGET_NR_set_tid_address) && defined(__NR_set_tid_address)
    case TARGET_NR_set_tid_address:
        return get_errno(set_tid_address((int *)g2h(arg1)));
#endif

    case TARGET_NR_tkill:
        return get_errno(safe_tkill((int)arg1, target_to_host_signal(arg2)));

    case TARGET_NR_tgkill:
        return get_errno(safe_tgkill((int)arg1, (int)arg2,
                         target_to_host_signal(arg3)));

#ifdef TARGET_NR_set_robust_list
    case TARGET_NR_set_robust_list:
    case TARGET_NR_get_robust_list:
        /* The ABI for supporting robust futexes has userspace pass
         * the kernel a pointer to a linked list which is updated by
         * userspace after the syscall; the list is walked by the kernel
         * when the thread exits. Since the linked list in QEMU guest
         * memory isn't a valid linked list for the host and we have
         * no way to reliably intercept the thread-death event, we can't
         * support these. Silently return ENOSYS so that guest userspace
         * falls back to a non-robust futex implementation (which should
         * be OK except in the corner case of the guest crashing while
         * holding a mutex that is shared with another process via
         * shared memory).
         */
        return -TARGET_ENOSYS;
#endif

#if defined(TARGET_NR_utimensat)
    case TARGET_NR_utimensat:
        {
            struct timespec *tsp, ts[2];
            if (!arg3) {
                tsp = NULL;
            } else {
                target_to_host_timespec(ts, arg3);
                target_to_host_timespec(ts+1, arg3+sizeof(struct target_timespec));
                tsp = ts;
            }
            if (!arg2)
                ret = get_errno(sys_utimensat(arg1, NULL, tsp, arg4));
            else {
                if (!(p = lock_user_string(arg2))) {
                    return -TARGET_EFAULT;
                }
                ret = get_errno(sys_utimensat(arg1, path(p), tsp, arg4));
                unlock_user(p, arg2, 0);
            }
        }
        return ret;
#endif
    case TARGET_NR_futex:
        return do_futex(arg1, arg2, arg3, arg4, arg5, arg6);
#if defined(TARGET_NR_inotify_init) && defined(__NR_inotify_init)
    case TARGET_NR_inotify_init:
        ret = get_errno(sys_inotify_init());
        if (ret >= 0) {
            fd_trans_register(ret, &target_inotify_trans);
        }
        return ret;
#endif
#ifdef CONFIG_INOTIFY1
#if defined(TARGET_NR_inotify_init1) && defined(__NR_inotify_init1)
    case TARGET_NR_inotify_init1:
        ret = get_errno(sys_inotify_init1(target_to_host_bitmask(arg1,
                                          fcntl_flags_tbl)));
        if (ret >= 0) {
            fd_trans_register(ret, &target_inotify_trans);
        }
        return ret;
#endif
#endif
#if defined(TARGET_NR_inotify_add_watch) && defined(__NR_inotify_add_watch)
    case TARGET_NR_inotify_add_watch:
        p = lock_user_string(arg2);
        ret = get_errno(sys_inotify_add_watch(arg1, path(p), arg3));
        unlock_user(p, arg2, 0);
        return ret;
#endif
#if defined(TARGET_NR_inotify_rm_watch) && defined(__NR_inotify_rm_watch)
    case TARGET_NR_inotify_rm_watch:
        return get_errno(sys_inotify_rm_watch(arg1, arg2));
#endif

#if defined(TARGET_NR_mq_open) && defined(__NR_mq_open)
    case TARGET_NR_mq_open:
        {
            struct mq_attr posix_mq_attr;
            struct mq_attr *pposix_mq_attr;
            int host_flags;

            host_flags = target_to_host_bitmask(arg2, fcntl_flags_tbl);
            pposix_mq_attr = NULL;
            if (arg4) {
                if (copy_from_user_mq_attr(&posix_mq_attr, arg4) != 0) {
                    return -TARGET_EFAULT;
                }
                pposix_mq_attr = &posix_mq_attr;
            }
            p = lock_user_string(arg1 - 1);
            if (!p) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(mq_open(p, host_flags, arg3, pposix_mq_attr));
            unlock_user (p, arg1, 0);
        }
        return ret;

    case TARGET_NR_mq_unlink:
        p = lock_user_string(arg1 - 1);
        if (!p) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(mq_unlink(p));
        unlock_user (p, arg1, 0);
        return ret;

    case TARGET_NR_mq_timedsend:
        {
            struct timespec ts;

            p = lock_user (VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                target_to_host_timespec(&ts, arg5);
                ret = get_errno(safe_mq_timedsend(arg1, p, arg3, arg4, &ts));
                host_to_target_timespec(arg5, &ts);
            } else {
                ret = get_errno(safe_mq_timedsend(arg1, p, arg3, arg4, NULL));
            }
            unlock_user (p, arg2, arg3);
        }
        return ret;

    case TARGET_NR_mq_timedreceive:
        {
            struct timespec ts;
            unsigned int prio;

            p = lock_user (VERIFY_READ, arg2, arg3, 1);
            if (arg5 != 0) {
                target_to_host_timespec(&ts, arg5);
                ret = get_errno(safe_mq_timedreceive(arg1, p, arg3,
                                                     &prio, &ts));
                host_to_target_timespec(arg5, &ts);
            } else {
                ret = get_errno(safe_mq_timedreceive(arg1, p, arg3,
                                                     &prio, NULL));
            }
            unlock_user (p, arg2, arg3);
            if (arg4 != 0)
                put_user_u32(prio, arg4);
        }
        return ret;

    /* Not implemented for now... */
/*     case TARGET_NR_mq_notify: */
/*         break; */

    case TARGET_NR_mq_getsetattr:
        {
            struct mq_attr posix_mq_attr_in, posix_mq_attr_out;
            ret = 0;
            if (arg2 != 0) {
                copy_from_user_mq_attr(&posix_mq_attr_in, arg2);
                ret = get_errno(mq_setattr(arg1, &posix_mq_attr_in,
                                           &posix_mq_attr_out));
            } else if (arg3 != 0) {
                ret = get_errno(mq_getattr(arg1, &posix_mq_attr_out));
            }
            if (ret == 0 && arg3 != 0) {
                copy_to_user_mq_attr(arg3, &posix_mq_attr_out);
            }
        }
        return ret;
#endif

#ifdef CONFIG_SPLICE
#ifdef TARGET_NR_tee
    case TARGET_NR_tee:
        {
            ret = get_errno(tee(arg1,arg2,arg3,arg4));
        }
        return ret;
#endif
#ifdef TARGET_NR_splice
    case TARGET_NR_splice:
        {
            loff_t loff_in, loff_out;
            loff_t *ploff_in = NULL, *ploff_out = NULL;
            if (arg2) {
                if (get_user_u64(loff_in, arg2)) {
                    return -TARGET_EFAULT;
                }
                ploff_in = &loff_in;
            }
            if (arg4) {
                if (get_user_u64(loff_out, arg4)) {
                    return -TARGET_EFAULT;
                }
                ploff_out = &loff_out;
            }
            ret = get_errno(splice(arg1, ploff_in, arg3, ploff_out, arg5, arg6));
            if (arg2) {
                if (put_user_u64(loff_in, arg2)) {
                    return -TARGET_EFAULT;
                }
            }
            if (arg4) {
                if (put_user_u64(loff_out, arg4)) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
#endif
#ifdef TARGET_NR_vmsplice
	case TARGET_NR_vmsplice:
        {
            struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);
            if (vec != NULL) {
                ret = get_errno(vmsplice(arg1, vec, arg3, arg4));
                unlock_iovec(vec, arg2, arg3, 0);
            } else {
                ret = -host_to_target_errno(errno);
            }
        }
        return ret;
#endif
#endif /* CONFIG_SPLICE */
#ifdef CONFIG_EVENTFD
#if defined(TARGET_NR_eventfd)
    case TARGET_NR_eventfd:
        ret = get_errno(eventfd(arg1, 0));
        if (ret >= 0) {
            fd_trans_register(ret, &target_eventfd_trans);
        }
        return ret;
#endif
#if defined(TARGET_NR_eventfd2)
    case TARGET_NR_eventfd2:
    {
        int host_flags = arg2 & (~(TARGET_O_NONBLOCK | TARGET_O_CLOEXEC));
        if (arg2 & TARGET_O_NONBLOCK) {
            host_flags |= O_NONBLOCK;
        }
        if (arg2 & TARGET_O_CLOEXEC) {
            host_flags |= O_CLOEXEC;
        }
        ret = get_errno(eventfd(arg1, host_flags));
        if (ret >= 0) {
            fd_trans_register(ret, &target_eventfd_trans);
        }
        return ret;
    }
#endif
#endif /* CONFIG_EVENTFD  */
#if defined(CONFIG_FALLOCATE) && defined(TARGET_NR_fallocate)
    case TARGET_NR_fallocate:
#if TARGET_ABI_BITS == 32
        ret = get_errno(fallocate(arg1, arg2, target_offset64(arg3, arg4),
                                  target_offset64(arg5, arg6)));
#else
        ret = get_errno(fallocate(arg1, arg2, arg3, arg4));
#endif
        return ret;
#endif
#if defined(CONFIG_SYNC_FILE_RANGE)
#if defined(TARGET_NR_sync_file_range)
    case TARGET_NR_sync_file_range:
#if TARGET_ABI_BITS == 32
#if defined(TARGET_MIPS)
        ret = get_errno(sync_file_range(arg1, target_offset64(arg3, arg4),
                                        target_offset64(arg5, arg6), arg7));
#else
        ret = get_errno(sync_file_range(arg1, target_offset64(arg2, arg3),
                                        target_offset64(arg4, arg5), arg6));
#endif /* !TARGET_MIPS */
#else
        ret = get_errno(sync_file_range(arg1, arg2, arg3, arg4));
#endif
        return ret;
#endif
#if defined(TARGET_NR_sync_file_range2)
    case TARGET_NR_sync_file_range2:
        /* This is like sync_file_range but the arguments are reordered */
#if TARGET_ABI_BITS == 32
        ret = get_errno(sync_file_range(arg1, target_offset64(arg3, arg4),
                                        target_offset64(arg5, arg6), arg2));
#else
        ret = get_errno(sync_file_range(arg1, arg3, arg4, arg2));
#endif
        return ret;
#endif
#endif
#if defined(TARGET_NR_signalfd4)
    case TARGET_NR_signalfd4:
        return do_signalfd4(arg1, arg2, arg4);
#endif
#if defined(TARGET_NR_signalfd)
    case TARGET_NR_signalfd:
        return do_signalfd4(arg1, arg2, 0);
#endif
#if defined(CONFIG_EPOLL)
#if defined(TARGET_NR_epoll_create)
    case TARGET_NR_epoll_create:
        return get_errno(epoll_create(arg1));
#endif
#if defined(TARGET_NR_epoll_create1) && defined(CONFIG_EPOLL_CREATE1)
    case TARGET_NR_epoll_create1:
        return get_errno(epoll_create1(arg1));
#endif
#if defined(TARGET_NR_epoll_ctl)
    case TARGET_NR_epoll_ctl:
    {
        struct epoll_event ep;
        struct epoll_event *epp = 0;
        if (arg4) {
            struct target_epoll_event *target_ep;
            if (!lock_user_struct(VERIFY_READ, target_ep, arg4, 1)) {
                return -TARGET_EFAULT;
            }
            ep.events = tswap32(target_ep->events);
            /* The epoll_data_t union is just opaque data to the kernel,
             * so we transfer all 64 bits across and need not worry what
             * actual data type it is.
             */
            ep.data.u64 = tswap64(target_ep->data.u64);
            unlock_user_struct(target_ep, arg4, 0);
            epp = &ep;
        }
        return get_errno(epoll_ctl(arg1, arg2, arg3, epp));
    }
#endif

#if defined(TARGET_NR_epoll_wait) || defined(TARGET_NR_epoll_pwait)
#if defined(TARGET_NR_epoll_wait)
    case TARGET_NR_epoll_wait:
#endif
#if defined(TARGET_NR_epoll_pwait)
    case TARGET_NR_epoll_pwait:
#endif
    {
        struct target_epoll_event *target_ep;
        struct epoll_event *ep;
        int epfd = arg1;
        int maxevents = arg3;
        int timeout = arg4;

        if (maxevents <= 0 || maxevents > TARGET_EP_MAX_EVENTS) {
            return -TARGET_EINVAL;
        }

        target_ep = lock_user(VERIFY_WRITE, arg2,
                              maxevents * sizeof(struct target_epoll_event), 1);
        if (!target_ep) {
            return -TARGET_EFAULT;
        }

        ep = g_try_new(struct epoll_event, maxevents);
        if (!ep) {
            unlock_user(target_ep, arg2, 0);
            return -TARGET_ENOMEM;
        }

        switch (num) {
#if defined(TARGET_NR_epoll_pwait)
        case TARGET_NR_epoll_pwait:
        {
            target_sigset_t *target_set;
            sigset_t _set, *set = &_set;

            if (arg5) {
                if (arg6 != sizeof(target_sigset_t)) {
                    ret = -TARGET_EINVAL;
                    break;
                }

                target_set = lock_user(VERIFY_READ, arg5,
                                       sizeof(target_sigset_t), 1);
                if (!target_set) {
                    ret = -TARGET_EFAULT;
                    break;
                }
                target_to_host_sigset(set, target_set);
                unlock_user(target_set, arg5, 0);
            } else {
                set = NULL;
            }

            ret = get_errno(safe_epoll_pwait(epfd, ep, maxevents, timeout,
                                             set, SIGSET_T_SIZE));
            break;
        }
#endif
#if defined(TARGET_NR_epoll_wait)
        case TARGET_NR_epoll_wait:
            ret = get_errno(safe_epoll_pwait(epfd, ep, maxevents, timeout,
                                             NULL, 0));
            break;
#endif
        default:
            ret = -TARGET_ENOSYS;
        }
        if (!is_error(ret)) {
            int i;
            for (i = 0; i < ret; i++) {
                target_ep[i].events = tswap32(ep[i].events);
                target_ep[i].data.u64 = tswap64(ep[i].data.u64);
            }
            unlock_user(target_ep, arg2,
                        ret * sizeof(struct target_epoll_event));
        } else {
            unlock_user(target_ep, arg2, 0);
        }
        g_free(ep);
        return ret;
    }
#endif
#endif
#ifdef TARGET_NR_prlimit64
    case TARGET_NR_prlimit64:
    {
        /* args: pid, resource number, ptr to new rlimit, ptr to old rlimit */
        struct target_rlimit64 *target_rnew, *target_rold;
        struct host_rlimit64 rnew, rold, *rnewp = 0;
        int resource = target_to_host_resource(arg2);
        if (arg3) {
            if (!lock_user_struct(VERIFY_READ, target_rnew, arg3, 1)) {
                return -TARGET_EFAULT;
            }
            rnew.rlim_cur = tswap64(target_rnew->rlim_cur);
            rnew.rlim_max = tswap64(target_rnew->rlim_max);
            unlock_user_struct(target_rnew, arg3, 0);
            rnewp = &rnew;
        }

        ret = get_errno(sys_prlimit64(arg1, resource, rnewp, arg4 ? &rold : 0));
        if (!is_error(ret) && arg4) {
            if (!lock_user_struct(VERIFY_WRITE, target_rold, arg4, 1)) {
                return -TARGET_EFAULT;
            }
            target_rold->rlim_cur = tswap64(rold.rlim_cur);
            target_rold->rlim_max = tswap64(rold.rlim_max);
            unlock_user_struct(target_rold, arg4, 1);
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_gethostname
    case TARGET_NR_gethostname:
    {
        char *name = lock_user(VERIFY_WRITE, arg1, arg2, 0);
        if (name) {
            ret = get_errno(gethostname(name, arg2));
            unlock_user(name, arg1, arg2);
        } else {
            ret = -TARGET_EFAULT;
        }
        return ret;
    }
#endif
#ifdef TARGET_NR_atomic_cmpxchg_32
    case TARGET_NR_atomic_cmpxchg_32:
    {
        /* should use start_exclusive from main.c */
        abi_ulong mem_value;
        if (get_user_u32(mem_value, arg6)) {
            target_siginfo_t info;
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = arg6;
            queue_signal((CPUArchState *)cpu_env, info.si_signo,
                         QEMU_SI_FAULT, &info);
            ret = 0xdeadbeef;

        }
        if (mem_value == arg2)
            put_user_u32(arg1, arg6);
        return mem_value;
    }
#endif
#ifdef TARGET_NR_atomic_barrier
    case TARGET_NR_atomic_barrier:
        /* Like the kernel implementation and the
           qemu arm barrier, no-op this? */
        return 0;
#endif

#ifdef TARGET_NR_timer_create
    case TARGET_NR_timer_create:
    {
        /* args: clockid_t clockid, struct sigevent *sevp, timer_t *timerid */

        struct sigevent host_sevp = { {0}, }, *phost_sevp = NULL;

        int clkid = arg1;
        int timer_index = next_free_host_timer();

        if (timer_index < 0) {
            ret = -TARGET_EAGAIN;
        } else {
            timer_t *phtimer = g_posix_timers  + timer_index;

            if (arg2) {
                phost_sevp = &host_sevp;
                ret = target_to_host_sigevent(phost_sevp, arg2);
                if (ret != 0) {
                    return ret;
                }
            }

            ret = get_errno(timer_create(clkid, phost_sevp, phtimer));
            if (ret) {
                phtimer = NULL;
            } else {
                if (put_user(TIMER_MAGIC | timer_index, arg3, target_timer_t)) {
                    return -TARGET_EFAULT;
                }
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_settime
    case TARGET_NR_timer_settime:
    {
        /* args: timer_t timerid, int flags, const struct itimerspec *new_value,
         * struct itimerspec * old_value */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else if (arg3 == 0) {
            ret = -TARGET_EINVAL;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            struct itimerspec hspec_new = {{0},}, hspec_old = {{0},};

            if (target_to_host_itimerspec(&hspec_new, arg3)) {
                return -TARGET_EFAULT;
            }
            ret = get_errno(
                          timer_settime(htimer, arg2, &hspec_new, &hspec_old));
            if (arg4 && host_to_target_itimerspec(arg4, &hspec_old)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_gettime
    case TARGET_NR_timer_gettime:
    {
        /* args: timer_t timerid, struct itimerspec *curr_value */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else if (!arg2) {
            ret = -TARGET_EFAULT;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            struct itimerspec hspec;
            ret = get_errno(timer_gettime(htimer, &hspec));

            if (host_to_target_itimerspec(arg2, &hspec)) {
                ret = -TARGET_EFAULT;
            }
        }
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_getoverrun
    case TARGET_NR_timer_getoverrun:
    {
        /* args: timer_t timerid */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            ret = get_errno(timer_getoverrun(htimer));
        }
        fd_trans_unregister(ret);
        return ret;
    }
#endif

#ifdef TARGET_NR_timer_delete
    case TARGET_NR_timer_delete:
    {
        /* args: timer_t timerid */
        target_timer_t timerid = get_timer_id(arg1);

        if (timerid < 0) {
            ret = timerid;
        } else {
            timer_t htimer = g_posix_timers[timerid];
            ret = get_errno(timer_delete(htimer));
            g_posix_timers[timerid] = 0;
        }
        return ret;
    }
#endif

#if defined(TARGET_NR_timerfd_create) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_create:
        return get_errno(timerfd_create(arg1,
                          target_to_host_bitmask(arg2, fcntl_flags_tbl)));
#endif

#if defined(TARGET_NR_timerfd_gettime) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_gettime:
        {
            struct itimerspec its_curr;

            ret = get_errno(timerfd_gettime(arg1, &its_curr));

            if (arg2 && host_to_target_itimerspec(arg2, &its_curr)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_timerfd_settime) && defined(CONFIG_TIMERFD)
    case TARGET_NR_timerfd_settime:
        {
            struct itimerspec its_new, its_old, *p_new;

            if (arg3) {
                if (target_to_host_itimerspec(&its_new, arg3)) {
                    return -TARGET_EFAULT;
                }
                p_new = &its_new;
            } else {
                p_new = NULL;
            }

            ret = get_errno(timerfd_settime(arg1, arg2, p_new, &its_old));

            if (arg4 && host_to_target_itimerspec(arg4, &its_old)) {
                return -TARGET_EFAULT;
            }
        }
        return ret;
#endif

#if defined(TARGET_NR_ioprio_get) && defined(__NR_ioprio_get)
    case TARGET_NR_ioprio_get:
        return get_errno(ioprio_get(arg1, arg2));
#endif

#if defined(TARGET_NR_ioprio_set) && defined(__NR_ioprio_set)
    case TARGET_NR_ioprio_set:
        return get_errno(ioprio_set(arg1, arg2, arg3));
#endif

#if defined(TARGET_NR_setns) && defined(CONFIG_SETNS)
    case TARGET_NR_setns:
        return get_errno(setns(arg1, arg2));
#endif
#if defined(TARGET_NR_unshare) && defined(CONFIG_SETNS)
    case TARGET_NR_unshare:
        return get_errno(unshare(arg1));
#endif
#if defined(TARGET_NR_kcmp) && defined(__NR_kcmp)
    case TARGET_NR_kcmp:
        return get_errno(kcmp(arg1, arg2, arg3, arg4, arg5));
#endif
#ifdef TARGET_NR_swapcontext
    case TARGET_NR_swapcontext:
        /* PowerPC specific.  */
        return do_swapcontext(cpu_env, arg1, arg2, arg3);
#endif

    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported syscall: %d\n", num);
        return -TARGET_ENOSYS;
    }
    return ret;
}

/* Emit the signature for a SyscallArgsFn.  */
#define SYSCALL_ARGS(NAME) \
    static const SyscallDef *args_##NAME(const SyscallDef *def, \
                                         int64_t out[6], abi_long in[8])

/* Emit the signature for a SyscallImplFn.  */
#define SYSCALL_IMPL(NAME) \
    static abi_long impl_##NAME(CPUArchState *cpu_env, int64_t arg1, \
                                int64_t arg2, int64_t arg3, int64_t arg4, \
                                int64_t arg5, int64_t arg6)

#include "syscall-fcntl.inc.c"
#include "syscall-file.inc.c"
#include "syscall-ioctl.inc.c"
#include "syscall-ipc.inc.c"
#include "syscall-mem.inc.c"
#include "syscall-proc.inc.c"
#include "syscall-sig.inc.c"
#include "syscall-time.inc.c"

#undef SYSCALL_IMPL
#undef SYSCALL_ARGS

/*
 * Emit a complete SyscallDef structure.
 */
#define SYSCALL_DEF_FULL(NAME, ...) \
    static const SyscallDef def_##NAME = { .name = #NAME, __VA_ARGS__ }

/*
 * Emit the definition for a "simple" syscall.  Such does not use
 * SyscallArgsFn and only uses arg_type for strace.
 */
#define SYSCALL_DEF(NAME, ...) \
    SYSCALL_DEF_FULL(NAME, .impl = impl_##NAME, .arg_type = { __VA_ARGS__ })

/* Similarly, but also uses an args hook.  */
#define SYSCALL_DEF_ARGS(NAME, ...) \
    SYSCALL_DEF_FULL(NAME, .impl = impl_##NAME, .args = args_##NAME, \
                     .arg_type = { __VA_ARGS__ })

#include "syscall-defs.h"

#undef SYSCALL_DEF
#undef SYSCALL_DEF_ARGS
#undef SYSCALL_DEF_FULL

static const SyscallDef *syscall_table(int num)
{
#define SYSCALL_DEF(NAME, ...)  case TARGET_NR_##NAME: return &def_##NAME
#define SYSCALL_DEF_ARGS(NAME, ...)  SYSCALL_DEF(NAME)
#define SYSCALL_DEF_FULL(NAME, ...)  SYSCALL_DEF(NAME)
#define SYSCALL_TABLE

    switch (num) {
#include "syscall-defs.h"
    }
    return NULL;

#undef SYSCALL_DEF
#undef SYSCALL_DEF_ARGS
#undef SYSCALL_DEF_FULL
#undef SYSCALL_TABLE
}

abi_long do_syscall(void *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    const SyscallDef *def, *orig_def;
    abi_long raw_args[8] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 };
    int64_t  out_args[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
    abi_long ret;

#ifdef DEBUG_ERESTARTSYS
    /* Debug-only code for exercising the syscall-restart code paths
     * in the per-architecture cpu main loops: restart every syscall
     * the guest makes once before letting it through.
     */
    {
        static bool flag;
        flag = !flag;
        if (flag) {
            return -TARGET_ERESTARTSYS;
        }
    }
#endif

    trace_guest_user_syscall(cpu, num, arg1, arg2, arg3, arg4,
                             arg5, arg6, arg7, arg8);

    orig_def = def = syscall_table(num);
    if (def == NULL) {
        /* Unconverted.  */
        if (unlikely(do_strace)) {
            print_syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);
            ret = do_syscall1(cpu_env, num, arg1, arg2, arg3, arg4,
                              arg5, arg6, arg7, arg8);
            print_syscall_ret(num, ret);
        } else {
            ret = do_syscall1(cpu_env, num, arg1, arg2, arg3, arg4,
                              arg5, arg6, arg7, arg8);
        }
        goto fini;
    }

    if (def->args) {
        def = def->args(def, out_args, raw_args);
        if (unlikely(def == NULL)) {
            ret = -host_to_target_errno(errno);
            if (unlikely(do_strace)) {
                print_syscall_def(orig_def, out_args);
                print_syscall_def_ret(orig_def, ret);
            }
            goto fini;
        }
    }

    if (unlikely(do_strace)) {
        print_syscall_def(def, out_args);
        ret = def->impl(cpu_env, out_args[0], out_args[1], out_args[2],
                        out_args[3], out_args[4], out_args[5]);
        print_syscall_def_ret(def, ret);
    } else {
        ret = def->impl(cpu_env, out_args[0], out_args[1], out_args[2],
                        out_args[3], out_args[4], out_args[5]);
    }

 fini:
    trace_guest_user_syscall_ret(cpu, num, ret);
    return ret;
}

void syscall_init(void)
{
    const argtype *arg_type;
    int size;
    int i;

    thunk_init(STRUCT_MAX);

#define STRUCT(name, ...) \
    thunk_register_struct(STRUCT_ ## name, #name, struct_ ## name ## _def);
#define STRUCT_SPECIAL(name) \
    thunk_register_struct_direct(STRUCT_ ## name, #name, \
                                 &struct_ ## name ## _def);

#include "syscall_types.h"

#undef STRUCT
#undef STRUCT_SPECIAL

    /*
     * Build target_to_host_errno_table[] table from
     * host_to_target_errno_table[].
     */
    for (i = 0; i < ERRNO_TABLE_SIZE; i++) {
        target_to_host_errno_table[host_to_target_errno_table[i]] = i;
    }

    /*
     * We patch the ioctl size if necessary.  We rely on the fact that
     * no ioctl has all the bits at '1' in the size field.
     */
    for (i = 0; i < ARRAY_SIZE(ioctl_entries); i++) {
        IOCTLEntry *ie = &ioctl_entries[i];
        if (((ie->target_cmd >> TARGET_IOC_SIZESHIFT) & TARGET_IOC_SIZEMASK) ==
            TARGET_IOC_SIZEMASK) {
            arg_type = ie->arg_type;
            if (arg_type[0] != TYPE_PTR) {
                fprintf(stderr, "cannot patch size for ioctl 0x%x\n",
                        ie->target_cmd);
                exit(1);
            }
            arg_type++;
            size = thunk_type_size(arg_type, 0);
            ie->target_cmd = (ie->target_cmd &
                              ~(TARGET_IOC_SIZEMASK << TARGET_IOC_SIZESHIFT)) |
                (size << TARGET_IOC_SIZESHIFT);
        }

        /* automatic consistency check if same arch */
#if (defined(__i386__) && defined(TARGET_I386) && defined(TARGET_ABI32)) || \
    (defined(__x86_64__) && defined(TARGET_X86_64))
        if (unlikely(ie->target_cmd != ie->host_cmd)) {
            fprintf(stderr, "ERROR: ioctl(%s): target=0x%x host=0x%x\n",
                    ie->name, ie->target_cmd, ie->host_cmd);
        }
#endif
    }
}
