/*
 *  System call related declarations
 *
 *  Copyright (c) 2013-15 Stacey D. Son (sson at FreeBSD)
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

#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include <sys/syscall.h>
#include <sys/resource.h>

#include "errno_defs.h"

#include "freebsd/syscall_nr.h"
#include "netbsd/syscall_nr.h"
#include "openbsd/syscall_nr.h"

/*
 * machine/_types.h
 * or x86/_types.h
 */

/*
 * time_t seems to be very inconsistly defined for the different *BSD's...
 *
 * FreeBSD uses a 64bits time_t except on i386
 * so we have to add a special case here.
 *
 * On NetBSD time_t is always defined as an int64_t.  On OpenBSD time_t
 * is always defined as an int.
 *
 */
#if (!defined(TARGET_I386))
typedef int64_t target_freebsd_time_t;
#else
typedef int32_t target_freebsd_time_t;
#endif

struct target_iovec {
    abi_long iov_base;   /* Starting address */
    abi_long iov_len;   /* Number of bytes */
};

/*
 *  sys/mman.h
 */
#define TARGET_FREEBSD_MAP_RESERVED0080 0x0080  /* previously misimplemented */
                                                /* MAP_INHERIT */
#define TARGET_FREEBSD_MAP_RESERVED0100 0x0100  /* previously unimplemented */
                                                /* MAP_NOEXTEND */
#define TARGET_FREEBSD_MAP_STACK        0x0400  /* region grows down, like a */
                                                /* stack */
#define TARGET_FREEBSD_MAP_NOSYNC       0x0800  /* page to but do not sync */
                                                /* underlying file */

#define TARGET_FREEBSD_MAP_FLAGMASK     0x1ff7

#define TARGET_NETBSD_MAP_INHERIT       0x0080  /* region is retained after */
                                                /* exec */
#define TARGET_NETBSD_MAP_TRYFIXED      0x0400  /* attempt hint address, even */
                                                /* within break */
#define TARGET_NETBSD_MAP_WIRED         0x0800  /* mlock() mapping when it is */
                                                /* established */

#define TARGET_NETBSD_MAP_STACK         0x2000  /* allocated from memory, */
                                                /* swap space (stack) */

#define TARGET_NETBSD_MAP_FLAGMASK      0x3ff7

#define TARGET_OPENBSD_MAP_INHERIT      0x0080  /* region is retained after */
                                                /* exec */
#define TARGET_OPENBSD_MAP_NOEXTEND     0x0100  /* for MAP_FILE, don't change */
                                                /* file size */
#define TARGET_OPENBSD_MAP_TRYFIXED     0x0400  /* attempt hint address, */
                                                /* even within heap */

#define TARGET_OPENBSD_MAP_FLAGMASK     0x17f7

/* XXX */
#define TARGET_BSD_MAP_FLAGMASK         0x3ff7

/*
 * sys/time.h
 * sys/timex.h
 */

typedef abi_long target_freebsd_suseconds_t;

/* compare to sys/timespec.h */
struct target_freebsd_timespec {
    target_freebsd_time_t   tv_sec;     /* seconds */
    abi_long                tv_nsec;    /* and nanoseconds */
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

#define TARGET_CPUCLOCK_WHICH_PID   0
#define TARGET_CPUCLOCK_WHICH_TID   1

/* sys/umtx.h */
struct target_freebsd__umtx_time {
    struct target_freebsd_timespec  _timeout;
    uint32_t    _flags;
    uint32_t    _clockid;
};

struct target_freebsd_timeval {
    target_freebsd_time_t       tv_sec; /* seconds */
    target_freebsd_suseconds_t  tv_usec;/* and microseconds */
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

/*
 *  sys/resource.h
 */
#if defined(__FreeBSD__)
#define TARGET_RLIM_INFINITY    RLIM_INFINITY
#else
#define TARGET_RLIM_INFINITY    ((abi_ulong)-1)
#endif

#define TARGET_RLIMIT_CPU       0
#define TARGET_RLIMIT_FSIZE     1
#define TARGET_RLIMIT_DATA      2
#define TARGET_RLIMIT_STACK     3
#define TARGET_RLIMIT_CORE      4
#define TARGET_RLIMIT_RSS       5
#define TARGET_RLIMIT_MEMLOCK   6
#define TARGET_RLIMIT_NPROC     7
#define TARGET_RLIMIT_NOFILE    8
#define TARGET_RLIMIT_SBSIZE    9
#define TARGET_RLIMIT_AS        10
#define TARGET_RLIMIT_NPTS      11
#define TARGET_RLIMIT_SWAP      12

struct target_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

struct target_freebsd_rusage {
    struct target_freebsd_timeval ru_utime; /* user time used */
    struct target_freebsd_timeval ru_stime; /* system time used */
    abi_long    ru_maxrss;      /* maximum resident set size */
    abi_long    ru_ixrss;       /* integral shared memory size */
    abi_long    ru_idrss;       /* integral unshared data size */
    abi_long    ru_isrss;       /* integral unshared stack size */
    abi_long    ru_minflt;      /* page reclaims */
    abi_long    ru_majflt;      /* page faults */
    abi_long    ru_nswap;       /* swaps */
    abi_long    ru_inblock;     /* block input operations */
    abi_long    ru_oublock;     /* block output operations */
    abi_long    ru_msgsnd;      /* messages sent */
    abi_long    ru_msgrcv;      /* messages received */
    abi_long    ru_nsignals;    /* signals received */
    abi_long    ru_nvcsw;       /* voluntary context switches */
    abi_long    ru_nivcsw;      /* involuntary context switches */
};

struct target_freebsd__wrusage {
    struct target_freebsd_rusage wru_self;
    struct target_freebsd_rusage wru_children;
};

/*
 * sys/socket.h
 */

/*
 * Types
 */
#define TARGET_SOCK_STREAM      1   /* stream socket */
#define TARGET_SOCK_DGRAM       2   /* datagram socket */
#define TARGET_SOCK_RAW         3   /* raw-protocol interface */
#define TARGET_SOCK_RDM         4   /* reliably-delivered message */
#define TARGET_SOCK_SEQPACKET   5   /* sequenced packet stream */


/*
 * Option flags per-socket.
 */

#define TARGET_SO_DEBUG         0x0001  /* turn on debugging info recording */
#define TARGET_SO_ACCEPTCONN    0x0002  /* socket has had listen() */
#define TARGET_SO_REUSEADDR     0x0004  /* allow local address reuse */
#define TARGET_SO_KEEPALIVE     0x0008  /* keep connections alive */
#define TARGET_SO_DONTROUTE     0x0010  /* just use interface addresses */
#define TARGET_SO_BROADCAST     0x0020  /* permit sending of broadcast msgs */
#define TARGET_SO_USELOOPBACK   0x0040  /* bypass hardware when possible */
#define TARGET_SO_LINGER        0x0080  /* linger on close if data present */
#define TARGET_SO_OOBINLINE     0x0100  /* leave received OOB data in line */
#define TARGET_SO_REUSEPORT     0x0200  /* allow local address & port reuse */
#define TARGET_SO_TIMESTAMP     0x0400  /* timestamp received dgram traffic */
#define TARGET_SO_NOSIGPIPE     0x0800  /* no SIGPIPE from EPIPE */
#define TARGET_SO_ACCEPTFILTER  0x1000  /* there is an accept filter */
#define TARGET_SO_BINTIME       0x2000  /* timestamp received dgram traffic */
#define TARGET_SO_NO_OFFLOAD    0x4000  /* socket cannot be offloaded */
#define TARGET_SO_NO_DDP        0x8000  /* disable direct data placement */

/*
 * Additional options, not kept in so_options.
 */
#define TARGET_SO_SNDBUF        0x1001  /* send buffer size */
#define TARGET_SO_RCVBUF        0x1002  /* receive buffer size */
#define TARGET_SO_SNDLOWAT      0x1003  /* send low-water mark */
#define TARGET_SO_RCVLOWAT      0x1004  /* receive low-water mark */
#define TARGET_SO_SNDTIMEO      0x1005  /* send timeout */
#define TARGET_SO_RCVTIMEO      0x1006  /* receive timeout */
#define TARGET_SO_ERROR         0x1007  /* get error status and clear */
#define TARGET_SO_TYPE          0x1008  /* get socket type */
#define TARGET_SO_LABEL         0x1009  /* socket's MAC label */
#define TARGET_SO_PEERLABEL     0x1010  /* socket's peer's MAC label */
#define TARGET_SO_LISTENQLIMIT  0x1011  /* socket's backlog limit */
#define TARGET_SO_LISTENQLEN    0x1012  /* socket's complete queue length */
#define TARGET_SO_LISTENINCQLEN 0x1013  /* socket's incomplete queue length */
#define TARGET_SO_SETFIB        0x1014  /* use this FIB to route */
#define TARGET_SO_USER_COOKIE   0x1015  /* user cookie (dummynet etc.) */
#define TARGET_SO_PROTOCOL      0x1016  /* get socket protocol (Linux name) */

/* alias for SO_PROTOCOL (SunOS name) */
#define TARGET_SO_PROTOTYPE     TARGET_SO_PROTOCOL

/*
 * Level number for (get/set)sockopt() to apply to socket itself.
 */
#define TARGET_SOL_SOCKET       0xffff  /* options for socket level */

#ifndef CMSG_ALIGN
#define CMSG_ALIGN(len) (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#endif

/*
 * sys/socket.h
 */
struct target_msghdr {
    abi_long    msg_name;       /* Socket name */
    int32_t     msg_namelen;    /* Length of name */
    abi_long    msg_iov;        /* Data blocks */
    int32_t     msg_iovlen;     /* Number of blocks */
    abi_long    msg_control;    /* Per protocol magic (eg BSD fd passing) */
    int32_t     msg_controllen; /* Length of cmsg list */
    int32_t     msg_flags;      /* flags on received message */
};

struct target_sockaddr {
    uint8_t sa_len;
    uint8_t sa_family;
    uint8_t sa_data[14];
} QEMU_PACKED;

struct target_in_addr {
    uint32_t s_addr; /* big endian */
};

struct target_cmsghdr {
    uint32_t    cmsg_len;
    int32_t     cmsg_level;
    int32_t     cmsg_type;
};

/*
 * mips32 is the exception to the general rule of long-alignment; it
 * unconditionally uses 64-bit alignment instead.
 */
#if defined(TARGET_MIPS) && TARGET_ABI_BITS == 32
#define TARGET_ALIGNBYTES   (sizeof(abi_llong) - 1)
#else
#define TARGET_ALIGNBYTES   (sizeof(abi_long) - 1)
#endif

#define TARGET_CMSG_NXTHDR(mhdr, cmsg, cmsg_start) \
                               __target_cmsg_nxthdr(mhdr, cmsg, cmsg_start)
#define TARGET_CMSG_ALIGN(len) (((len) + TARGET_ALIGNBYTES) \
                               & (size_t) ~TARGET_ALIGNBYTES)
#define TARGET_CMSG_DATA(cmsg) \
    ((unsigned char *)(cmsg) + TARGET_CMSG_ALIGN(sizeof(struct target_cmsghdr)))
#define TARGET_CMSG_SPACE(len) \
    (TARGET_CMSG_ALIGN(sizeof(struct target_cmsghdr)) + TARGET_CMSG_ALIGN(len))
#define TARGET_CMSG_LEN(len) \
    (TARGET_CMSG_ALIGN(sizeof(struct target_cmsghdr)) + (len))

static inline struct target_cmsghdr *
__target_cmsg_nxthdr(struct target_msghdr *__mhdr,
                     struct target_cmsghdr *__cmsg,
                     struct target_cmsghdr *__cmsg_start)
{
    struct target_cmsghdr *__ptr;

    __ptr = (struct target_cmsghdr *)((unsigned char *) __cmsg +
        TARGET_CMSG_ALIGN(tswap32(__cmsg->cmsg_len)));
    if ((unsigned long)((char *)(__ptr + 1) - (char *)__cmsg_start) >
        tswap32(__mhdr->msg_controllen)) {
        /* No more entries.  */
        return (struct target_cmsghdr *)0;
    }
    return __ptr;
}

/*
 * netinet/in.h
 */
struct target_ip_mreq {
    struct target_in_addr   imr_multiaddr;
    struct target_in_addr   imr_interface;
};

struct target_ip_mreqn {
    struct target_in_addr   imr_multiaddr;
    struct target_in_addr   imr_address;
    int32_t                 imr_ifindex;
};

#define safe_syscall0(type, name) \
type safe_##name(void) \
{ \
    return safe_syscall(SYS_##name); \
}

#define safe_syscall1(type, name, type1, arg1) \
type safe_##name(type1 arg1) \
{ \
    return safe_syscall(SYS_##name, arg1); \
}

#define safe_syscall2(type, name, type1, arg1, type2, arg2) \
type safe_##name(type1 arg1, type2 arg2) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2); \
}

#define safe_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3); \
}

#define safe_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4); \
}

#define safe_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4, arg5); \
}

#define safe_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5, type6, arg6) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5, type6 arg6) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4, arg5, arg6); \
}

/* So far all target and host bitmasks are the same */
#define target_to_host_bitmask(x, tbl) (x)
#define host_to_target_bitmask(x, tbl) (x)

#endif /* SYSCALL_DEFS_H */
