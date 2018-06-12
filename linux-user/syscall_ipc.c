/*
 *  Linux ipc-related syscalls
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
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include "qemu.h"
#include "syscall.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <linux/unistd.h>


#ifdef __NR_msgsnd
safe_syscall4(int, msgsnd, int, msgid, const void *, msgp, size_t, sz,
              int, flags)
safe_syscall5(int, msgrcv, int, msgid, void *, msgp, size_t, sz,
              long, msgtype, int, flags)
safe_syscall4(int, semtimedop, int, semid, struct sembuf *, tsops,
              unsigned, nsops, const struct timespec *, timeout)
#else
/* This host kernel architecture uses a single ipc syscall; fake up
 * wrappers for the sub-operations to hide this implementation detail.
 * Annoyingly we can't include linux/ipc.h to get the constant definitions
 * for the call parameter because some structs in there conflict with the
 * sys/ipc.h ones. So we just define them here, and rely on them being
 * the same for all host architectures.
 */
#define Q_SEMTIMEDOP 4
#define Q_MSGSND 11
#define Q_MSGRCV 12
#define Q_IPCCALL(VERSION, OP) ((VERSION) << 16 | (OP))

safe_syscall6(int, ipc, int, call, long, first, long, second, long, third,
              void *, ptr, long, fifth)

static int safe_msgsnd(int msgid, const void *msgp, size_t sz, int flags)
{
    return safe_ipc(Q_IPCCALL(0, Q_MSGSND), msgid, sz, flags, (void *)msgp, 0);
}

static int safe_msgrcv(int msgid, void *msgp, size_t sz, long type, int flags)
{
    return safe_ipc(Q_IPCCALL(1, Q_MSGRCV), msgid, sz, flags, msgp, type);
}

static int safe_semtimedop(int semid, struct sembuf *tsops, unsigned nsops,
                           const struct timespec *timeout)
{
    return safe_ipc(Q_IPCCALL(0, Q_SEMTIMEDOP), semid, nsops, 0, tsops,
                    (long)timeout);
}
#endif

/* See <linux/ipc.h> comment above.  */
#define SEMOPM  500

#define N_SHM_REGIONS  32

static struct shm_region {
    abi_ulong start;
    abi_ulong size;
    bool in_use;
} shm_regions[N_SHM_REGIONS];

#ifndef TARGET_SEMID64_DS
/* asm-generic version of this struct */
struct target_semid64_ds {
    struct target_ipc_perm sem_perm;
    abi_ulong sem_otime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused1;
#endif
    abi_ulong sem_ctime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused2;
#endif
    abi_ulong sem_nsems;
    abi_ulong __unused3;
    abi_ulong __unused4;
};
#endif

static abi_long target_to_host_ipc_perm(struct ipc_perm *host_ip,
                                        abi_ulong target_addr)
{
    struct target_ipc_perm *target_ip;
    struct target_semid64_ds *target_sd;

    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    target_ip = &target_sd->sem_perm;
    host_ip->__key = tswap32(target_ip->__key);
    host_ip->uid = tswap32(target_ip->uid);
    host_ip->gid = tswap32(target_ip->gid);
    host_ip->cuid = tswap32(target_ip->cuid);
    host_ip->cgid = tswap32(target_ip->cgid);
#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_PPC)
    host_ip->mode = tswap32(target_ip->mode);
#else
    host_ip->mode = tswap16(target_ip->mode);
#endif
#if defined(TARGET_PPC)
    host_ip->__seq = tswap32(target_ip->__seq);
#else
    host_ip->__seq = tswap16(target_ip->__seq);
#endif
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static abi_long host_to_target_ipc_perm(abi_ulong target_addr,
                                        struct ipc_perm *host_ip)
{
    struct target_ipc_perm *target_ip;
    struct target_semid64_ds *target_sd;

    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    target_ip = &target_sd->sem_perm;
    target_ip->__key = tswap32(host_ip->__key);
    target_ip->uid = tswap32(host_ip->uid);
    target_ip->gid = tswap32(host_ip->gid);
    target_ip->cuid = tswap32(host_ip->cuid);
    target_ip->cgid = tswap32(host_ip->cgid);
#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_PPC)
    target_ip->mode = tswap32(host_ip->mode);
#else
    target_ip->mode = tswap16(host_ip->mode);
#endif
#if defined(TARGET_PPC)
    target_ip->__seq = tswap32(host_ip->__seq);
#else
    target_ip->__seq = tswap16(host_ip->__seq);
#endif
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

static abi_long target_to_host_semid_ds(struct semid_ds *host_sd,
                                        abi_ulong target_addr)
{
    struct target_semid64_ds *target_sd;

    if (target_to_host_ipc_perm(&host_sd->sem_perm, target_addr)) {
        return -TARGET_EFAULT;
    }
    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    host_sd->sem_nsems = tswapal(target_sd->sem_nsems);
    host_sd->sem_otime = tswapal(target_sd->sem_otime);
    host_sd->sem_ctime = tswapal(target_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static abi_long host_to_target_semid_ds(abi_ulong target_addr,
                                        struct semid_ds *host_sd)
{
    struct target_semid64_ds *target_sd;

    if (host_to_target_ipc_perm(target_addr, &host_sd->sem_perm)) {
        return -TARGET_EFAULT;
    }
    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    target_sd->sem_nsems = tswapal(host_sd->sem_nsems);
    target_sd->sem_otime = tswapal(host_sd->sem_otime);
    target_sd->sem_ctime = tswapal(host_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

struct target_seminfo {
    int semmap;
    int semmni;
    int semmns;
    int semmnu;
    int semmsl;
    int semopm;
    int semume;
    int semusz;
    int semvmx;
    int semaem;
};

static abi_long host_to_target_seminfo(abi_ulong target_addr,
                                       struct seminfo *host_seminfo)
{
    struct target_seminfo *target_seminfo;

    if (!lock_user_struct(VERIFY_WRITE, target_seminfo, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_seminfo->semmap, &target_seminfo->semmap);
    __put_user(host_seminfo->semmni, &target_seminfo->semmni);
    __put_user(host_seminfo->semmns, &target_seminfo->semmns);
    __put_user(host_seminfo->semmnu, &target_seminfo->semmnu);
    __put_user(host_seminfo->semmsl, &target_seminfo->semmsl);
    __put_user(host_seminfo->semopm, &target_seminfo->semopm);
    __put_user(host_seminfo->semume, &target_seminfo->semume);
    __put_user(host_seminfo->semusz, &target_seminfo->semusz);
    __put_user(host_seminfo->semvmx, &target_seminfo->semvmx);
    __put_user(host_seminfo->semaem, &target_seminfo->semaem);
    unlock_user_struct(target_seminfo, target_addr, 1);
    return 0;
}

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

union target_semun {
    int val;
    abi_ulong buf;
    abi_ulong array;
    abi_ulong __buf;
};

static abi_long target_to_host_semarray(int semid,
                                        unsigned short **host_array,
                                        abi_ulong target_addr)
{
    int nsems;
    unsigned short *array;
    union semun semun;
    struct semid_ds semid_ds;
    int i, ret;

    semun.buf = &semid_ds;

    ret = semctl(semid, 0, IPC_STAT, semun);
    if (ret == -1) {
        return get_errno(ret);
    }

    nsems = semid_ds.sem_nsems;

    *host_array = g_try_new(unsigned short, nsems);
    if (!*host_array) {
        return -TARGET_ENOMEM;
    }
    array = lock_user(VERIFY_READ, target_addr,
                      nsems * sizeof(unsigned short), 1);
    if (!array) {
        g_free(*host_array);
        return -TARGET_EFAULT;
    }
    for (i = 0; i < nsems; i++) {
        __get_user((*host_array)[i], &array[i]);
    }
    unlock_user(array, target_addr, 0);

    return 0;
}

static abi_long host_to_target_semarray(int semid, abi_ulong target_addr,
                                        unsigned short **host_array)
{
    int nsems;
    unsigned short *array;
    union semun semun;
    struct semid_ds semid_ds;
    int i, ret;

    semun.buf = &semid_ds;

    ret = semctl(semid, 0, IPC_STAT, semun);
    if (ret == -1) {
        return get_errno(ret);
    }

    nsems = semid_ds.sem_nsems;

    array = lock_user(VERIFY_WRITE, target_addr,
                      nsems * sizeof(unsigned short), 0);
    if (!array) {
        return -TARGET_EFAULT;
    }
    for (i = 0; i < nsems; i++) {
        __put_user((*host_array)[i], &array[i]);
    }
    g_free(*host_array);
    unlock_user(array, target_addr, 1);

    return 0;
}

struct target_sembuf {
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

static abi_long target_to_host_sembuf(struct sembuf *host_sembuf,
                                      abi_ulong target_addr,
                                      unsigned nsops)
{
    struct target_sembuf *target_sembuf;
    int i;

    target_sembuf = lock_user(VERIFY_READ, target_addr,
                              nsops * sizeof(struct target_sembuf), 1);
    if (!target_sembuf) {
        return -TARGET_EFAULT;
    }
    for (i = 0; i < nsops; i++) {
        __get_user(host_sembuf[i].sem_num, &target_sembuf[i].sem_num);
        __get_user(host_sembuf[i].sem_op, &target_sembuf[i].sem_op);
        __get_user(host_sembuf[i].sem_flg, &target_sembuf[i].sem_flg);
    }
    unlock_user(target_sembuf, target_addr, 0);
    return 0;
}

struct target_msqid_ds {
    struct target_ipc_perm msg_perm;
    abi_ulong msg_stime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused1;
#endif
    abi_ulong msg_rtime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused2;
#endif
    abi_ulong msg_ctime;
#if TARGET_ABI_BITS == 32
    abi_ulong __unused3;
#endif
    abi_ulong __msg_cbytes;
    abi_ulong msg_qnum;
    abi_ulong msg_qbytes;
    abi_ulong msg_lspid;
    abi_ulong msg_lrpid;
    abi_ulong __unused4;
    abi_ulong __unused5;
};

static abi_long target_to_host_msqid_ds(struct msqid_ds *host_md,
                                        abi_ulong target_addr)
{
    struct target_msqid_ds *target_md;

    if (target_to_host_ipc_perm(&host_md->msg_perm, target_addr)) {
        return -TARGET_EFAULT;
    }
    if (!lock_user_struct(VERIFY_READ, target_md, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    host_md->msg_stime = tswapal(target_md->msg_stime);
    host_md->msg_rtime = tswapal(target_md->msg_rtime);
    host_md->msg_ctime = tswapal(target_md->msg_ctime);
    host_md->__msg_cbytes = tswapal(target_md->__msg_cbytes);
    host_md->msg_qnum = tswapal(target_md->msg_qnum);
    host_md->msg_qbytes = tswapal(target_md->msg_qbytes);
    host_md->msg_lspid = tswapal(target_md->msg_lspid);
    host_md->msg_lrpid = tswapal(target_md->msg_lrpid);
    unlock_user_struct(target_md, target_addr, 0);
    return 0;
}

static abi_long host_to_target_msqid_ds(abi_ulong target_addr,
                                        struct msqid_ds *host_md)
{
    struct target_msqid_ds *target_md;

    if (host_to_target_ipc_perm(target_addr, &host_md->msg_perm)) {
        return -TARGET_EFAULT;
    }
    if (!lock_user_struct(VERIFY_WRITE, target_md, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    target_md->msg_stime = tswapal(host_md->msg_stime);
    target_md->msg_rtime = tswapal(host_md->msg_rtime);
    target_md->msg_ctime = tswapal(host_md->msg_ctime);
    target_md->__msg_cbytes = tswapal(host_md->__msg_cbytes);
    target_md->msg_qnum = tswapal(host_md->msg_qnum);
    target_md->msg_qbytes = tswapal(host_md->msg_qbytes);
    target_md->msg_lspid = tswapal(host_md->msg_lspid);
    target_md->msg_lrpid = tswapal(host_md->msg_lrpid);
    unlock_user_struct(target_md, target_addr, 1);
    return 0;
}

struct target_msginfo {
    int msgpool;
    int msgmap;
    int msgmax;
    int msgmnb;
    int msgmni;
    int msgssz;
    int msgtql;
    unsigned short int msgseg;
};

static abi_long host_to_target_msginfo(abi_ulong target_addr,
                                       struct msginfo *host_msginfo)
{
    struct target_msginfo *target_msginfo;

    if (!lock_user_struct(VERIFY_WRITE, target_msginfo, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_msginfo->msgpool, &target_msginfo->msgpool);
    __put_user(host_msginfo->msgmap, &target_msginfo->msgmap);
    __put_user(host_msginfo->msgmax, &target_msginfo->msgmax);
    __put_user(host_msginfo->msgmnb, &target_msginfo->msgmnb);
    __put_user(host_msginfo->msgmni, &target_msginfo->msgmni);
    __put_user(host_msginfo->msgssz, &target_msginfo->msgssz);
    __put_user(host_msginfo->msgtql, &target_msginfo->msgtql);
    __put_user(host_msginfo->msgseg, &target_msginfo->msgseg);
    unlock_user_struct(target_msginfo, target_addr, 1);
    return 0;
}

struct target_msgbuf {
    abi_long mtype;
    char mtext[1];
};

static abi_long target_to_host_shmid_ds(struct shmid_ds *host_sd,
                                        abi_ulong target_addr)
{
    struct target_shmid_ds *target_sd;

    if (target_to_host_ipc_perm(&host_sd->shm_perm, target_addr)) {
        return -TARGET_EFAULT;
    }
    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_sd->shm_segsz, &target_sd->shm_segsz);
    __get_user(host_sd->shm_atime, &target_sd->shm_atime);
    __get_user(host_sd->shm_dtime, &target_sd->shm_dtime);
    __get_user(host_sd->shm_ctime, &target_sd->shm_ctime);
    __get_user(host_sd->shm_cpid, &target_sd->shm_cpid);
    __get_user(host_sd->shm_lpid, &target_sd->shm_lpid);
    __get_user(host_sd->shm_nattch, &target_sd->shm_nattch);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

static abi_long host_to_target_shmid_ds(abi_ulong target_addr,
                                        struct shmid_ds *host_sd)
{
    struct target_shmid_ds *target_sd;

    if (host_to_target_ipc_perm(target_addr, &host_sd->shm_perm)) {
        return -TARGET_EFAULT;
    }
    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_sd->shm_segsz, &target_sd->shm_segsz);
    __put_user(host_sd->shm_atime, &target_sd->shm_atime);
    __put_user(host_sd->shm_dtime, &target_sd->shm_dtime);
    __put_user(host_sd->shm_ctime, &target_sd->shm_ctime);
    __put_user(host_sd->shm_cpid, &target_sd->shm_cpid);
    __put_user(host_sd->shm_lpid, &target_sd->shm_lpid);
    __put_user(host_sd->shm_nattch, &target_sd->shm_nattch);
    unlock_user_struct(target_sd, target_addr, 1);
    return 0;
}

struct target_shminfo {
    abi_ulong shmmax;
    abi_ulong shmmin;
    abi_ulong shmmni;
    abi_ulong shmseg;
    abi_ulong shmall;
};

static abi_long host_to_target_shminfo(abi_ulong target_addr,
                                       struct shminfo *host_shminfo)
{
    struct target_shminfo *target_shminfo;

    if (!lock_user_struct(VERIFY_WRITE, target_shminfo, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_shminfo->shmmax, &target_shminfo->shmmax);
    __put_user(host_shminfo->shmmin, &target_shminfo->shmmin);
    __put_user(host_shminfo->shmmni, &target_shminfo->shmmni);
    __put_user(host_shminfo->shmseg, &target_shminfo->shmseg);
    __put_user(host_shminfo->shmall, &target_shminfo->shmall);
    unlock_user_struct(target_shminfo, target_addr, 1);
    return 0;
}

struct target_shm_info {
    int used_ids;
    abi_ulong shm_tot;
    abi_ulong shm_rss;
    abi_ulong shm_swp;
    abi_ulong swap_attempts;
    abi_ulong swap_successes;
};

static abi_long host_to_target_shm_info(abi_ulong target_addr,
                                        struct shm_info *host_shm_info)
{
    struct target_shm_info *target_shm_info;

    if (!lock_user_struct(VERIFY_WRITE, target_shm_info, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_shm_info->used_ids, &target_shm_info->used_ids);
    __put_user(host_shm_info->shm_tot, &target_shm_info->shm_tot);
    __put_user(host_shm_info->shm_rss, &target_shm_info->shm_rss);
    __put_user(host_shm_info->shm_swp, &target_shm_info->shm_swp);
    __put_user(host_shm_info->swap_attempts,
               &target_shm_info->swap_attempts);
    __put_user(host_shm_info->swap_successes,
               &target_shm_info->swap_successes);
    unlock_user_struct(target_shm_info, target_addr, 1);
    return 0;
}

#ifndef TARGET_FORCE_SHMLBA
/* For most architectures, SHMLBA is the same as the page size;
 * some architectures have larger values, in which case they should
 * define TARGET_FORCE_SHMLBA and provide a target_shmlba() function.
 * This corresponds to the kernel arch code defining __ARCH_FORCE_SHMLBA
 * and defining its own value for SHMLBA.
 *
 * The kernel also permits SHMLBA to be set by the architecture to a
 * value larger than the page size without setting __ARCH_FORCE_SHMLBA;
 * this means that addresses are rounded to the large size if
 * SHM_RND is set but addresses not aligned to that size are not rejected
 * as long as they are at least page-aligned. Since the only architecture
 * which uses this is ia64 this code doesn't provide for that oddity.
 */
static abi_ulong target_shmlba(CPUArchState *cpu_env)
{
    return TARGET_PAGE_SIZE;
}
#endif


SYSCALL_IMPL(msgctl)
{
    abi_long msgid = arg1;
    int cmd = arg2 & 0xff;
    abi_ulong ptr = arg3;
    struct msqid_ds dsarg;
    struct msginfo msginfo;
    abi_long ret;

    switch (cmd) {
    case IPC_STAT:
    case IPC_SET:
    case MSG_STAT:
        if (target_to_host_msqid_ds(&dsarg, ptr)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(msgctl(msgid, cmd, &dsarg));
        if (!is_error(ret) && host_to_target_msqid_ds(ptr, &dsarg)) {
            return -TARGET_EFAULT;
        }
        return ret;

    case IPC_RMID:
        return get_errno(msgctl(msgid, cmd, NULL));

    case IPC_INFO:
    case MSG_INFO:
        ret = get_errno(msgctl(msgid, cmd, (struct msqid_ds *)&msginfo));
        if (host_to_target_msginfo(ptr, &msginfo)) {
            return -TARGET_EFAULT;
        }
        return ret;

    default:
        return -TARGET_EINVAL;
    }
}
SYSCALL_DEF(msgctl, ARG_DEC, ARG_DEC, ARG_PTR);

SYSCALL_IMPL(msgget)
{
    return get_errno(msgget(arg1, arg2));
}
SYSCALL_DEF(msgget, ARG_DEC, ARG_DEC);

SYSCALL_IMPL(msgrcv)
{
    int msqid = arg1;
    abi_ulong msgp = arg2;
    abi_long msgsz = arg3;
    abi_long msgtyp = arg4;
    int msgflg = arg5;
    struct target_msgbuf *target_mb;
    char *target_mtext;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (msgsz < 0) {
        return -TARGET_EINVAL;
    }
    if (!lock_user_struct(VERIFY_WRITE, target_mb, msgp, 0)) {
        return -TARGET_EFAULT;
    }

    host_mb = g_try_malloc(msgsz + sizeof(long));
    if (!host_mb) {
        ret = -TARGET_ENOMEM;
        goto end;
    }
    ret = get_errno(safe_msgrcv(msqid, host_mb, msgsz, msgtyp, msgflg));

    if (ret > 0) {
        abi_ulong target_mtext_addr = msgp + sizeof(abi_ulong);
        target_mtext = lock_user(VERIFY_WRITE, target_mtext_addr, ret, 0);
        if (!target_mtext) {
            ret = -TARGET_EFAULT;
            goto end;
        }
        memcpy(target_mb->mtext, host_mb->mtext, ret);
        unlock_user(target_mtext, target_mtext_addr, ret);
    }
    target_mb->mtype = tswapal(host_mb->mtype);

 end:
    unlock_user_struct(target_mb, msgp, 1);
    g_free(host_mb);
    return ret;
}
SYSCALL_DEF(msgrcv, ARG_DEC, ARG_PTR, ARG_DEC, ARG_DEC, ARG_HEX);

SYSCALL_IMPL(msgsnd)
{
    int msqid = arg1;
    abi_ulong msgp = arg2;
    abi_long msgsz = arg3;
    int msgflg = arg4;
    struct target_msgbuf *target_mb;
    struct msgbuf *host_mb;
    abi_long ret = 0;

    if (msgsz < 0) {
        return -TARGET_EINVAL;
    }
    if (!lock_user_struct(VERIFY_READ, target_mb, msgp, 0)) {
        return -TARGET_EFAULT;
    }
    host_mb = g_try_malloc(msgsz + sizeof(long));
    if (!host_mb) {
        unlock_user_struct(target_mb, msgp, 0);
        return -TARGET_ENOMEM;
    }

    host_mb->mtype = (abi_long)tswapal(target_mb->mtype);
    memcpy(host_mb->mtext, target_mb->mtext, msgsz);
    ret = get_errno(safe_msgsnd(msqid, host_mb, msgsz, msgflg));

    g_free(host_mb);
    unlock_user_struct(target_mb, msgp, 0);
    return ret;
}
SYSCALL_DEF(msgsnd, ARG_DEC, ARG_PTR, ARG_DEC, ARG_HEX);

SYSCALL_IMPL(semctl)
{
    abi_long semid = arg1;
    abi_long semnum = arg2;
    int cmd = arg3 & 0xff;
    abi_ulong target_arg = arg4;
    union target_semun target_su = { .buf = target_arg };
    union semun arg;
    struct semid_ds dsarg;
    unsigned short *array = NULL;
    struct seminfo seminfo;
    abi_long ret, err;

    switch (cmd) {
    case GETVAL:
    case SETVAL:
        /* In 64 bit cross-endian situations, we will erroneously pick up
         * the wrong half of the union for the "val" element.  To rectify
         * this, the entire 8-byte structure is byteswapped, followed by
         * a swap of the 4 byte val field. In other cases, the data is
         * already in proper host byte order. */
        if (sizeof(target_su.val) != sizeof(target_su.buf)) {
            target_su.buf = tswapal(target_su.buf);
            arg.val = tswap32(target_su.val);
        } else {
            arg.val = target_su.val;
        }
        return get_errno(semctl(semid, semnum, cmd, arg));

    case GETALL:
    case SETALL:
        err = target_to_host_semarray(semid, &array, target_su.array);
        if (err) {
            return err;
        }
        arg.array = array;
        ret = get_errno(semctl(semid, semnum, cmd, arg));
        if (!is_error(ret)) {
            err = host_to_target_semarray(semid, target_su.array, &array);
            if (err) {
                return err;
            }
        }
        return ret;

    case IPC_STAT:
    case IPC_SET:
    case SEM_STAT:
        err = target_to_host_semid_ds(&dsarg, target_su.buf);
        if (err) {
            return err;
        }
        arg.buf = &dsarg;
        ret = get_errno(semctl(semid, semnum, cmd, arg));
        if (!is_error(ret)) {
            err = host_to_target_semid_ds(target_su.buf, &dsarg);
            if (err) {
                return err;
            }
        }
        return ret;

    case IPC_INFO:
    case SEM_INFO:
        arg.__buf = &seminfo;
        ret = get_errno(semctl(semid, semnum, cmd, arg));
        if (!is_error(ret)) {
            err = host_to_target_seminfo(target_su.__buf, &seminfo);
            if (err) {
                return err;
            }
        }
        return ret;

    case IPC_RMID:
    case GETPID:
    case GETNCNT:
    case GETZCNT:
        return get_errno(semctl(semid, semnum, cmd, NULL));

    default:
        return -TARGET_EINVAL;
    }
}
SYSCALL_DEF(semctl, ARG_DEC, ARG_DEC, ARG_DEC, ARG_HEX);

SYSCALL_IMPL(semget)
{
    return get_errno(semget(arg1, arg2, arg3));
}
SYSCALL_DEF(semget, ARG_DEC, ARG_DEC, ARG_HEX);

SYSCALL_IMPL(semop)
{
    abi_long semid = arg1;
    abi_ulong ptr = arg2;
    abi_ulong nsops = arg3;
    struct sembuf sops[SEMOPM];

    if (nsops > SEMOPM) {
        return -TARGET_E2BIG;
    }
    if (target_to_host_sembuf(sops, ptr, nsops)) {
        return -TARGET_EFAULT;
    }
    return get_errno(safe_semtimedop(semid, sops, nsops, NULL));
}
SYSCALL_DEF(semop, ARG_DEC, ARG_PTR, ARG_DEC);

SYSCALL_IMPL(shmget)
{
    return get_errno(shmget(arg1, arg2, arg3));
}
SYSCALL_DEF(shmget, ARG_DEC, ARG_DEC, ARG_HEX);

SYSCALL_IMPL(shmctl)
{
    int shmid = arg1;
    int cmd = arg2 & 0xff;
    abi_ulong buf = arg3;
    struct shmid_ds dsarg;
    struct shminfo shminfo;
    struct shm_info shm_info;
    abi_long ret;

    switch (cmd) {
    case IPC_STAT:
    case IPC_SET:
    case SHM_STAT:
        if (target_to_host_shmid_ds(&dsarg, buf)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(shmctl(shmid, cmd, &dsarg));
        if (!is_error(ret) && host_to_target_shmid_ds(buf, &dsarg)) {
            return -TARGET_EFAULT;
        }
        return ret;

    case IPC_INFO:
        ret = get_errno(shmctl(shmid, cmd, (struct shmid_ds *)&shminfo));
        if (!is_error(ret) && host_to_target_shminfo(buf, &shminfo)) {
            return -TARGET_EFAULT;
        }
        return ret;

    case SHM_INFO:
        ret = get_errno(shmctl(shmid, cmd, (struct shmid_ds *)&shm_info));
        if (!is_error(ret) && host_to_target_shm_info(buf, &shm_info)) {
            return -TARGET_EFAULT;
        }
        return ret;

    case IPC_RMID:
    case SHM_LOCK:
    case SHM_UNLOCK:
        return get_errno(shmctl(shmid, cmd, NULL));

    default:
        return -TARGET_EINVAL;
    }
}
SYSCALL_DEF(shmctl, ARG_DEC, ARG_DEC, ARG_PTR);

SYSCALL_IMPL(shmat)
{
    int shmid = arg1;
    abi_ulong shmaddr = arg2;
    int shmflg = arg3;
    abi_ulong raddr;
    void *host_raddr;
    struct shmid_ds shm_info;
    int i, ret;
    abi_ulong shmlba;

    /* Find out the length of the shared memory segment.  */
    ret = get_errno(shmctl(shmid, IPC_STAT, &shm_info));
    if (is_error(ret)) {
        /* can't get length, bail out */
        return ret;
    }

    /* Validate memory placement and alignment for the guest.  */
    shmlba = target_shmlba(cpu_env);
    if (shmaddr & (shmlba - 1)) {
        if (shmflg & SHM_RND) {
            shmaddr &= ~(shmlba - 1);
        } else {
            return -TARGET_EINVAL;
        }
    }
    if (!guest_range_valid(shmaddr, shm_info.shm_segsz)) {
        return -TARGET_EINVAL;
    }

    mmap_lock();

    if (shmaddr) {
        host_raddr = shmat(shmid, (void *)g2h(shmaddr), shmflg);
    } else {
        abi_ulong mmap_start = mmap_find_vma(0, shm_info.shm_segsz);
        if (mmap_start == -1) {
            errno = ENOMEM;
            host_raddr = (void *)-1;
        } else {
            host_raddr = shmat(shmid, g2h(mmap_start), shmflg | SHM_REMAP);
        }
    }
    if (host_raddr == (void *)-1) {
        mmap_unlock();
        return get_errno((intptr_t)host_raddr);
    }

    raddr = h2g((uintptr_t)host_raddr);
    page_set_flags(raddr, raddr + shm_info.shm_segsz,
                   PAGE_VALID | PAGE_READ |
                   (shmflg & SHM_RDONLY ? 0 : PAGE_WRITE));

    for (i = 0; i < N_SHM_REGIONS; i++) {
        if (!shm_regions[i].in_use) {
            shm_regions[i].in_use = true;
            shm_regions[i].start = raddr;
            shm_regions[i].size = shm_info.shm_segsz;
            break;
        }
    }
    mmap_unlock();
    return raddr;
}

const SyscallDef def_shmat = {
    .name = "shmat",
    .impl = impl_shmat,
    .print_ret = print_syscall_ptr_ret,
    .arg_type = { ARG_DEC, ARG_PTR, ARG_HEX }
};

SYSCALL_IMPL(shmdt)
{
    abi_ulong shmaddr = arg1;
    abi_long ret;
    int i;

    mmap_lock();

    for (i = 0; i < N_SHM_REGIONS; ++i) {
        if (shm_regions[i].in_use && shm_regions[i].start == shmaddr) {
            shm_regions[i].in_use = false;
            page_set_flags(shmaddr, shmaddr + shm_regions[i].size, 0);
            break;
        }
    }
    ret = get_errno(shmdt(g2h(shmaddr)));

    mmap_unlock();

    return ret;
}
SYSCALL_DEF(shmdt, ARG_PTR);

#ifdef TARGET_NR_ipc
/* This differs from normal shmat in returning the result via a pointer.
 * Here we have shifted that pointer to arg4.
 */
SYSCALL_IMPL(ipc_shmat)
{
    abi_long ret = impl_shmat(cpu_env, arg1, arg2, arg3, 0, 0, 0);

    if (is_error(ret)) {
        return ret;
    }
    if (put_user_ual(ret, arg4)) {
        return -TARGET_EFAULT;
    }
    return 0;
}

static const SyscallDef def_ipc_shmat = {
    .name = "shmat",
    .impl = impl_ipc_shmat,
    .arg_type = { ARG_DEC, ARG_PTR, ARG_HEX, ARG_PTR },
};

/* Demultiplex the IPC syscall and shuffle the arguments around
 * into the "normal" ordering.
 */
SYSCALL_ARGS(ipc)
{
    int call = extract32(in[0], 0, 16);
    int version = extract32(in[0], 16, 16);
    abi_long first = in[1];
    abi_long second = in[2];
    abi_long third = in[3];
    abi_ulong ptr = in[4];
    abi_long fifth = in[5];
    abi_ulong atptr;

    /* IPC_* and SHM_* command values are the same on all linux platforms */
    switch (call) {
    case IPCOP_semop:
        out[0] = first;
        out[1] = ptr;
        out[2] = second;
        return &def_semop;

    case IPCOP_semget:
        out[0] = first;
        out[1] = second;
        out[2] = third;
        return &def_semget;

    case IPCOP_semctl:
        /* The semun argument to semctl is passed by value,
         * so dereference the ptr argument.
         */
        if (get_user_ual(atptr, ptr)) {
            errno = EFAULT;
            return NULL;
        }
        out[0] = first;
        out[1] = second;
        out[2] = third;
        out[3] = atptr;
        return &def_semctl;

    case IPCOP_msgget:
        out[0] = first;
        out[1] = second;
        return &def_msgget;

    case IPCOP_msgsnd:
        out[0] = first;
        out[1] = ptr;
        out[2] = second;
        out[3] = third;
        return &def_msgsnd;

    case IPCOP_msgctl:
        out[0] = first;
        out[1] = second;
        out[2] = ptr;
        return &def_msgctl;

    case IPCOP_msgrcv:
        if (version == 0) {
            struct target_ipc_kludge {
                abi_long msgp;
                abi_long msgtyp;
            } *tmp;

            if (!lock_user_struct(VERIFY_READ, tmp, ptr, 1)) {
                errno = EFAULT;
                return NULL;
            }
            out[0] = first;
            out[1] = tswapal(tmp->msgp);
            out[2] = second;
            out[3] = tswapal(tmp->msgtyp);
            out[4] = third;
            unlock_user_struct(tmp, ptr, 0);
        } else {
            out[0] = first;
            out[1] = ptr;
            out[2] = second;
            out[3] = fifth;
            out[4] = third;
        }
        return &def_msgrcv;

    case IPCOP_shmat:
        if (version == 1) {
            errno = EINVAL;
            return NULL;
        }
        out[0] = first;
        out[1] = ptr;
        out[2] = second;
        out[3] = third;
        return &def_ipc_shmat;

    case IPCOP_shmdt:
        out[0] = ptr;
        return &def_shmdt;

    case IPCOP_shmget:
        out[0] = first;
        out[1] = second;
        out[2] = third;
        return &def_shmget;

    case IPCOP_shmctl:
        out[0] = first;
        out[1] = second;
        out[2] = ptr;
        return &def_shmctl;

    default:
        /* Invalid syscall.  Continue to impl_ipc for logging.  */
        return def;
    }
}

SYSCALL_IMPL(ipc)
{
    int call = extract32(arg1, 0, 16);
    int version = extract32(arg1, 16, 16);

    gemu_log("Unsupported ipc call: %d (version %d)\n", call, version);
    return -TARGET_ENOSYS;
}
SYSCALL_DEF_ARGS(ipc, ARG_HEX, ARG_DEC, ARG_DEC, ARG_HEX, ARG_PTR, ARG_HEX);
#endif
