/*
 *  FreeBSD sysctl() and sysarch() system call emulation
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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
#include "qemu.h"
#include "qemu-bsd.h"
#include "target_arch_sysarch.h"
#include "signal-common.h"
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>   /* For struct kinfo_* */

#include "target_os_user.h"

/*
 * Length for the fixed length types.
 * 0 means variable length for strings and structures
 * Compare with sys/kern_sysctl.c ctl_size
 * Note: Not all types appear to be used in-tree.
 */
static const int guest_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(abi_int),
        [CTLTYPE_UINT] = sizeof(abi_uint),
        [CTLTYPE_LONG] = sizeof(abi_long),
        [CTLTYPE_ULONG] = sizeof(abi_ulong),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

static const int host_ctl_size[CTLTYPE + 1] = {
        [CTLTYPE_INT] = sizeof(int),
        [CTLTYPE_UINT] = sizeof(u_int),
        [CTLTYPE_LONG] = sizeof(long),
        [CTLTYPE_ULONG] = sizeof(u_long),
        [CTLTYPE_S8] = sizeof(int8_t),
        [CTLTYPE_S16] = sizeof(int16_t),
        [CTLTYPE_S32] = sizeof(int32_t),
        [CTLTYPE_S64] = sizeof(int64_t),
        [CTLTYPE_U8] = sizeof(uint8_t),
        [CTLTYPE_U16] = sizeof(uint16_t),
        [CTLTYPE_U32] = sizeof(uint32_t),
        [CTLTYPE_U64] = sizeof(uint64_t),
};

#ifdef TARGET_ABI32
/*
 * Limit the amount of available memory to be most of the 32-bit address
 * space. 0x100c000 was arrived at through trial and error as a good
 * definition of 'most'.
 */
static const abi_ulong guest_max_mem = UINT32_MAX - 0x100c000 + 1;

static abi_ulong cap_memory(uint64_t mem)
{
    return MIN(guest_max_mem, mem);
}
#endif

static abi_ulong scale_to_guest_pages(uint64_t pages)
{
    /* Scale pages from host to guest */
    pages = muldiv64(pages, qemu_real_host_page_size(), TARGET_PAGE_SIZE);
#ifdef TARGET_ABI32
    /* cap pages if need be */
    pages = MIN(pages, guest_max_mem / (abi_ulong)TARGET_PAGE_SIZE);
#endif
    return pages;
}

#ifdef TARGET_ABI32
/* Used only for TARGET_ABI32 */
static abi_long h2g_long_sat(long l)
{
    if (l > INT32_MAX) {
        l = INT32_MAX;
    } else if (l < INT32_MIN) {
        l = INT32_MIN;
    }
    return l;
}

static abi_ulong h2g_ulong_sat(u_long ul)
{
    return MIN(ul, UINT32_MAX);
}
#endif

/*
 * placeholder until bsd-user downstream upstreams this with its thread support
 */
#define bsd_get_ncpu() 1

static void
host_to_target_kinfo_proc(struct target_kinfo_proc *tki, struct kinfo_proc *hki)
{
    int i;

    __put_user(sizeof(struct target_kinfo_proc), &tki->ki_structsize);
    __put_user(hki->ki_layout, &tki->ki_layout);

    /* Some of these are used as flags (e.g. ki_fd == NULL in procstat). */
    tki->ki_args = tswapal((abi_ulong)(uintptr_t)hki->ki_args);
    tki->ki_paddr = tswapal((abi_ulong)(uintptr_t)hki->ki_paddr);
    tki->ki_addr = tswapal((abi_ulong)(uintptr_t)hki->ki_addr);
    tki->ki_tracep = tswapal((abi_ulong)(uintptr_t)hki->ki_tracep);
    tki->ki_textvp = tswapal((abi_ulong)(uintptr_t)hki->ki_textvp);
    tki->ki_fd = tswapal((abi_ulong)(uintptr_t)hki->ki_fd);
    tki->ki_vmspace = tswapal((abi_ulong)(uintptr_t)hki->ki_vmspace);
    tki->ki_wchan = tswapal((abi_ulong)(uintptr_t)hki->ki_wchan);

    __put_user(hki->ki_pid, &tki->ki_pid);
    __put_user(hki->ki_ppid, &tki->ki_ppid);
    __put_user(hki->ki_pgid, &tki->ki_pgid);
    __put_user(hki->ki_tpgid, &tki->ki_tpgid);
    __put_user(hki->ki_sid, &tki->ki_sid);
    __put_user(hki->ki_tsid, &tki->ki_tsid);
    __put_user(hki->ki_jobc, &tki->ki_jobc);
    __put_user(hki->ki_tdev, &tki->ki_tdev);

    host_to_target_sigset(&tki->ki_siglist, &hki->ki_siglist);
    host_to_target_sigset(&tki->ki_sigmask, &hki->ki_sigmask);
    host_to_target_sigset(&tki->ki_sigignore, &hki->ki_sigignore);
    host_to_target_sigset(&tki->ki_sigcatch, &hki->ki_sigcatch);

    __put_user(hki->ki_uid, &tki->ki_uid);
    __put_user(hki->ki_ruid, &tki->ki_ruid);
    __put_user(hki->ki_svuid, &tki->ki_svuid);
    __put_user(hki->ki_rgid, &tki->ki_rgid);
    __put_user(hki->ki_svgid, &tki->ki_svgid);
    __put_user(hki->ki_ngroups, &tki->ki_ngroups);

    for (i=0; i < TARGET_KI_NGROUPS; i++)
        __put_user(hki->ki_groups[i], &tki->ki_groups[i]);

    __put_user(hki->ki_size, &tki->ki_size);

    __put_user(hki->ki_rssize, &tki->ki_rssize);
    __put_user(hki->ki_swrss, &tki->ki_swrss);
    __put_user(hki->ki_tsize, &tki->ki_tsize);
    __put_user(hki->ki_dsize, &tki->ki_dsize);
    __put_user(hki->ki_ssize, &tki->ki_ssize);

    __put_user(hki->ki_xstat, &tki->ki_xstat);
    __put_user(hki->ki_acflag, &tki->ki_acflag);

    __put_user(hki->ki_pctcpu, &tki->ki_pctcpu);

    __put_user(hki->ki_estcpu, &tki->ki_estcpu);
    __put_user(hki->ki_slptime, &tki->ki_slptime);
    __put_user(hki->ki_swtime, &tki->ki_swtime);
    __put_user(hki->ki_cow, &tki->ki_cow);
    __put_user(hki->ki_runtime, &tki->ki_runtime);

    __put_user(hki->ki_start.tv_sec, &tki->ki_start.tv_sec);
    __put_user(hki->ki_start.tv_usec, &tki->ki_start.tv_usec);
    __put_user(hki->ki_childtime.tv_sec, &tki->ki_childtime.tv_sec);
    __put_user(hki->ki_childtime.tv_usec, &tki->ki_childtime.tv_usec);

    __put_user(hki->ki_flag, &tki->ki_flag);
    __put_user(hki->ki_kiflag, &tki->ki_kiflag);

    __put_user(hki->ki_traceflag, &tki->ki_traceflag);
    __put_user(hki->ki_stat, &tki->ki_stat);
    __put_user(hki->ki_nice, &tki->ki_nice);
    __put_user(hki->ki_lock, &tki->ki_lock);
    __put_user(hki->ki_rqindex, &tki->ki_rqindex);
    __put_user(hki->ki_oncpu_old, &tki->ki_oncpu_old);
    __put_user(hki->ki_lastcpu_old, &tki->ki_lastcpu_old);

    strncpy(tki->ki_tdname, hki->ki_tdname, TARGET_TDNAMLEN+1);
    strncpy(tki->ki_wmesg, hki->ki_wmesg, TARGET_WMESGLEN+1);
    strncpy(tki->ki_login, hki->ki_login, TARGET_LOGNAMELEN+1);
    strncpy(tki->ki_lockname, hki->ki_lockname, TARGET_LOCKNAMELEN+1);
    strncpy(tki->ki_comm, hki->ki_comm, TARGET_COMMLEN+1);
    strncpy(tki->ki_emul, hki->ki_emul, TARGET_KI_EMULNAMELEN+1);
    strncpy(tki->ki_loginclass, hki->ki_loginclass, TARGET_LOGINCLASSLEN+1);

    __put_user(hki->ki_oncpu, &tki->ki_oncpu);
    __put_user(hki->ki_lastcpu, &tki->ki_lastcpu);
    __put_user(hki->ki_tracer, &tki->ki_tracer);
    __put_user(hki->ki_flag2, &tki->ki_flag2);
    __put_user(hki->ki_fibnum, &tki->ki_fibnum);
    __put_user(hki->ki_cr_flags, &tki->ki_cr_flags);
    __put_user(hki->ki_jid, &tki->ki_jid);
    __put_user(hki->ki_numthreads, &tki->ki_numthreads);
    __put_user(hki->ki_tid, &tki->ki_tid);

    memcpy(&tki->ki_pri, &hki->ki_pri, sizeof(struct target_priority));

    h2g_rusage(&hki->ki_rusage, &tki->ki_rusage);
    h2g_rusage(&hki->ki_rusage_ch, &tki->ki_rusage_ch);

    __put_user(((uintptr_t)hki->ki_pcb), &tki->ki_pcb);
    __put_user(((uintptr_t)hki->ki_kstack), &tki->ki_kstack);
    __put_user(((uintptr_t)hki->ki_udata), &tki->ki_udata);
    __put_user(((uintptr_t)hki->ki_tdaddr), &tki->ki_tdaddr);

    __put_user(hki->ki_sflag, &tki->ki_sflag);
    __put_user(hki->ki_tdflags, &tki->ki_tdflags);
}

abi_long
do_sysctl_kern_getprocs(int op, int arg, size_t olen,
        struct target_kinfo_proc *tki, size_t *tlen)
{
    abi_long ret;
    struct kinfo_proc *kipp;
    int mib[4], num, i, miblen;
    size_t len;

    if (tlen == NULL)
        return -TARGET_EINVAL;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = op;
    mib[3] = arg;

    miblen = (op == KERN_PROC_ALL || op == KERN_PROC_PROC) ?  3 : 4;

    len = 0;
    ret = get_errno(sysctl(mib, miblen, NULL, &len, NULL, 0));
    if (is_error(ret))
        return ret;

    num = len / sizeof(*kipp);
    *tlen = num * sizeof(struct target_kinfo_proc);
    if (tki == NULL)
        return ret;

    if (olen < *tlen)
        return -TARGET_EINVAL;

    kipp = g_malloc(len);
    if (kipp == NULL)
        return -TARGET_ENOMEM;
    ret = get_errno(sysctl(mib, miblen, kipp, &len, NULL, 0));
    num = len / sizeof(*kipp);
    *tlen = num * sizeof(struct target_kinfo_proc);
    if (len % sizeof(*kipp) != 0 || kipp->ki_structsize != sizeof(*kipp)) {
        ret = -TARGET_EINVAL; /* XXX */
    } else if (!is_error(ret)) {
        for(i=0; i < num; i++)
            host_to_target_kinfo_proc(&tki[i], &kipp[i]);
    }

    g_free(kipp);
    return ret;
}

static void
host_to_target_kinfo_file(struct target_kinfo_file *tkif,
        struct kinfo_file *hkif)
{
    int type = hkif->kf_type;

    __put_user(hkif->kf_structsize, &tkif->kf_structsize);
    __put_user(hkif->kf_type, &tkif->kf_type);
    __put_user(hkif->kf_fd, &tkif->kf_fd);
    __put_user(hkif->kf_ref_count, &tkif->kf_ref_count);
    __put_user(hkif->kf_flags, &tkif->kf_flags);
    __put_user(hkif->kf_offset, &tkif->kf_offset);
    switch (type) {
    case TARGET_KF_TYPE_FIFO:
    case TARGET_KF_TYPE_SHM:
    case TARGET_KF_TYPE_VNODE:
        __put_user(hkif->kf_un.kf_file.kf_file_type,
                &tkif->kf_un.kf_file.kf_file_type);
        __put_user(hkif->kf_un.kf_file.kf_file_fsid,
                &tkif->kf_un.kf_file.kf_file_fsid);
        __put_user(hkif->kf_un.kf_file.kf_file_rdev,
                &tkif->kf_un.kf_file.kf_file_rdev);
        __put_user(hkif->kf_un.kf_file.kf_file_fileid,
                &tkif->kf_un.kf_file.kf_file_fileid);
        __put_user(hkif->kf_un.kf_file.kf_file_size,
                &tkif->kf_un.kf_file.kf_file_size);
        __put_user(hkif->kf_un.kf_file.kf_file_fsid_freebsd11,
                &tkif->kf_un.kf_file.kf_file_fsid_freebsd11);
        __put_user(hkif->kf_un.kf_file.kf_file_rdev_freebsd11,
                &tkif->kf_un.kf_file.kf_file_rdev_freebsd11);
        __put_user(hkif->kf_un.kf_file.kf_file_mode,
                &tkif->kf_un.kf_file.kf_file_mode);
        break;

    case TARGET_KF_TYPE_SOCKET:
        __put_user(hkif->kf_un.kf_sock.kf_sock_domain0,
                &tkif->kf_un.kf_sock.kf_sock_domain0);
        __put_user(hkif->kf_un.kf_sock.kf_sock_type0,
                &tkif->kf_un.kf_sock.kf_sock_type0);
        __put_user(hkif->kf_un.kf_sock.kf_sock_protocol0,
                &tkif->kf_un.kf_sock.kf_sock_protocol0);
/*  XXX - Implement copy function for sockaddr_storage
        host_to_target_copy_sockaddr_storage(
                &hkif->kf_un.kf_file.kf_sa_local,
                &kif->kf_un.kf_file.kf_sa_local);
        host_to_target_copy_sockaddr_storage(
                &hkif->kf_un.kf_file.kf_sa_peer,
                &kif->kf_un.kf_file.kf_sa_peer);
*/
        __put_user(hkif->kf_un.kf_sock.kf_sock_pcb,
                &tkif->kf_un.kf_sock.kf_sock_pcb);
        __put_user(hkif->kf_un.kf_sock.kf_sock_inpcb,
                &tkif->kf_un.kf_sock.kf_sock_inpcb);
        __put_user(hkif->kf_un.kf_sock.kf_sock_unpconn,
                &tkif->kf_un.kf_sock.kf_sock_unpconn);
        __put_user(hkif->kf_un.kf_sock.kf_sock_snd_sb_state,
                &tkif->kf_un.kf_sock.kf_sock_snd_sb_state);
        __put_user(hkif->kf_un.kf_sock.kf_sock_rcv_sb_state,
                &tkif->kf_un.kf_sock.kf_sock_rcv_sb_state);
        break;

    case TARGET_KF_TYPE_PIPE:
        __put_user(hkif->kf_un.kf_pipe.kf_pipe_addr,
                &tkif->kf_un.kf_pipe.kf_pipe_addr);
        __put_user(hkif->kf_un.kf_pipe.kf_pipe_peer,
                &tkif->kf_un.kf_pipe.kf_pipe_peer);
        __put_user(hkif->kf_un.kf_pipe.kf_pipe_buffer_cnt,
                &tkif->kf_un.kf_pipe.kf_pipe_buffer_cnt);
        break;

    case TARGET_KF_TYPE_SEM:
        __put_user(hkif->kf_un.kf_sem.kf_sem_value,
                &tkif->kf_un.kf_sem.kf_sem_value);
        __put_user(hkif->kf_un.kf_sem.kf_sem_mode,
                &tkif->kf_un.kf_sem.kf_sem_mode);
        break;

    case TARGET_KF_TYPE_PTS:
        __put_user(hkif->kf_un.kf_pts.kf_pts_dev_freebsd11,
                &tkif->kf_un.kf_pts.kf_pts_dev_freebsd11);
        __put_user(hkif->kf_un.kf_pts.kf_pts_dev,
                &tkif->kf_un.kf_pts.kf_pts_dev);
        break;

    case TARGET_KF_TYPE_PROCDESC:
        __put_user(hkif->kf_un.kf_proc.kf_pid,
                &tkif->kf_un.kf_proc.kf_pid);
        break;


    case TARGET_KF_TYPE_CRYPTO:
    case TARGET_KF_TYPE_KQUEUE:
    case TARGET_KF_TYPE_MQUEUE:
    case TARGET_KF_TYPE_NONE:
    case TARGET_KF_TYPE_UNKNOWN:
    default:
        /* Do nothing. */
        break;
    }
    __put_user(hkif->kf_status, &tkif->kf_status);
    for (int i = 0; i < (CAP_RIGHTS_VERSION + 2); i++)
        __put_user(hkif->kf_cap_rights.cr_rights[i],
                &tkif->kf_cap_rights.cr_rights[i]);
    strncpy(tkif->kf_path, hkif->kf_path, sizeof(tkif->kf_path));
}

abi_long
do_sysctl_kern_proc_filedesc(int pid, size_t olen,
        struct target_kinfo_file *tkif, size_t *tlen)
{
    abi_long ret;
    int mib[4], sz;
    size_t len;
    char *buf, *bp, *eb, *tp;
    struct kinfo_file *kf, kif;
    struct target_kinfo_file target_kif;

    if (tlen == NULL) {
        return -TARGET_EINVAL;
    }

    len = 0;
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_FILEDESC;
    mib[3] = pid;

    ret = get_errno(sysctl(mib, 4, NULL, &len, NULL, 0));
    if (is_error(ret)) {
        return ret;
    }
    if (tkif == NULL) {
        *tlen = len;
        return ret;
    }
    len = len * 4 / 3;
    buf = g_malloc(len);
    if (buf == NULL) {
        return -TARGET_ENOMEM;
    }

    /*
     * Count the number of records.
     *
     * Given that the kinfo_file information returned by
     * the kernel may be different sizes per record we have
     * to read it in and count the variable length records
     * by walking them.
     */
    ret = get_errno(sysctl(mib, 4, buf, &len, NULL, 0));
    if (is_error(ret)) {
        g_free(buf);
        return ret;
    }
    *tlen = len;
    bp = buf;
    eb = buf + len;
    while (bp < eb) {
        kf = (struct kinfo_file *)(uintptr_t)bp;
        bp += kf->kf_structsize;
    }
    if (olen < *tlen) {
        g_free(buf);
        return -TARGET_EINVAL;
    }

    /*
     * Unpack the records from the kernel into full length records
     * and byte swap, if needed.
     */
    bp = buf;
    eb = buf + len;
    tp = (char *)tkif;
    while (bp < eb) {
        kf = (struct kinfo_file *)(uintptr_t)bp;
        sz = kf->kf_structsize;
        /* Copy/expand into a zeroed buffer */
        memset(&kif, 0, sizeof(kif));
        memcpy(&kif, kf, sz);
        /* Byte swap and copy into a target buffer. */
        host_to_target_kinfo_file(&target_kif, &kif);
        /* Copy target buffer to user buffer and pack */
        memcpy(tp, &target_kif, sz);
        /* Advance to next packed record. */
        bp += sz;
        /* Advance to next packed, target record. */
        tp += sz;
    }

    g_free(buf);
    return ret;
}

static void
host_to_target_kinfo_vmentry(struct target_kinfo_vmentry *tkve,
        struct kinfo_vmentry *hkve)
{

    __put_user(hkve->kve_structsize, &tkve->kve_structsize);
    __put_user(hkve->kve_type, &tkve->kve_type);
    __put_user(hkve->kve_start, &tkve->kve_start);
    __put_user(hkve->kve_end, &tkve->kve_end);
    __put_user(hkve->kve_offset, &tkve->kve_offset);
    __put_user(hkve->kve_vn_fileid, &tkve->kve_vn_fileid);
    __put_user(hkve->kve_vn_fsid_freebsd11, &tkve->kve_vn_fsid_freebsd11);
    __put_user(hkve->kve_vn_fsid, &tkve->kve_vn_fsid);
    __put_user(hkve->kve_flags, &tkve->kve_flags);
    __put_user(hkve->kve_resident, &tkve->kve_resident);
    __put_user(hkve->kve_private_resident, &tkve->kve_private_resident);
    __put_user(hkve->kve_protection, &tkve->kve_protection);
    __put_user(hkve->kve_ref_count, &tkve->kve_ref_count);
    __put_user(hkve->kve_shadow_count, &tkve->kve_shadow_count);
    __put_user(hkve->kve_vn_type, &tkve->kve_vn_type);
    __put_user(hkve->kve_vn_size, &tkve->kve_vn_size);
    __put_user(hkve->kve_vn_rdev_freebsd11, &tkve->kve_vn_rdev_freebsd11);
    __put_user(hkve->kve_vn_rdev, &tkve->kve_vn_rdev);
    __put_user(hkve->kve_vn_mode, &tkve->kve_vn_mode);
    __put_user(hkve->kve_status, &tkve->kve_status);
    strncpy(tkve->kve_path, hkve->kve_path, sizeof(tkve->kve_path));
}

abi_long
do_sysctl_kern_proc_vmmap(int pid, size_t olen,
        struct target_kinfo_vmentry *tkve, size_t *tlen)
{
    abi_long ret;
    int mib[4], sz;
    size_t len;
    char *buf, *bp, *eb, *tp;
    struct kinfo_vmentry *kve, kvme;
    struct target_kinfo_vmentry target_kvme;

    if (tlen == NULL) {
        return -TARGET_EINVAL;
    }

    len = 0;
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_VMMAP;
    mib[3] = pid;

    ret = get_errno(sysctl(mib, 4, NULL, &len, NULL, 0));
    if (is_error(ret)) {
        return ret;
    }
    if (tkve == NULL) {
        *tlen = len;
        return ret;
    }
    len = len * 4 / 3;
    buf = g_malloc(len);
    if (buf == NULL) {
        return -TARGET_ENOMEM;
    }

    /*
     * Count the number of records.
     *
     * Given that the kinfo_file information returned by
     * the kernel may be differents sizes per record we have
     * to read it in and count the variable length records
     * by walking them.
     */
    ret = get_errno(sysctl(mib, 4, buf, &len, NULL, 0));
    if (is_error(ret)) {
        g_free(buf);
        return ret;
    }
    *tlen = len;
    bp = buf;
    eb = buf + len;
    while (bp < eb) {
        kve = (struct kinfo_vmentry *)(uintptr_t)bp;
        bp += kve->kve_structsize;
    }
    if (olen < *tlen) {
        g_free(buf);
        return -TARGET_EINVAL;
    }

    /*
     * Unpack the records from the kernel into full length records
     * and byte swap, if needed.
     */
    bp = buf;
    eb = buf + len;
    tp = (char *)tkve;
    while (bp < eb) {
        kve = (struct kinfo_vmentry *)(uintptr_t)bp;
        sz = kve->kve_structsize;
        /* Copy/expand into a zeroed buffer */
        memset(&kvme, 0, sizeof(kvme));
        memcpy(&kvme, kve, sz);
        /* Byte swap and copy into a target aligned buffer. */
        host_to_target_kinfo_vmentry(&target_kvme, &kvme);
        /* Copy target buffer to user buffer, packed. */
        memcpy(tp, &target_kvme, sz);
        /* Advance to next packed record. */
        bp += sz;
        /* Advance to next packed, target record. */
        tp += sz;
    }

    g_free(buf);
    return ret;
}

/*
 * This uses the undocumented oidfmt interface to find the kind of a requested
 * sysctl, see /sys/kern/kern_sysctl.c:sysctl_sysctl_oidfmt() (compare to
 * src/sbin/sysctl/sysctl.c)
 */
static int oidfmt(int *oid, int len, char *fmt, uint32_t *kind)
{
    int qoid[CTL_MAXNAME + 2];
    uint8_t buf[BUFSIZ];
    int i;
    size_t j;

    qoid[0] = CTL_SYSCTL;
    qoid[1] = CTL_SYSCTL_OIDFMT;
    memcpy(qoid + 2, oid, len * sizeof(int));

    j = sizeof(buf);
    i = sysctl(qoid, len + 2, buf, &j, 0, 0);
    if (i) {
        return i;
    }

    if (kind) {
        *kind = *(uint32_t *)buf;
    }

    if (fmt) {
        strcpy(fmt, (char *)(buf + sizeof(uint32_t)));
    }
    return 0;
}

/*
 * Convert the old value from host to guest.
 *
 * For LONG and ULONG on ABI32, we need to 'down convert' the 8 byte quantities
 * to 4 bytes. The caller setup a buffer in host memory to get this data from
 * the kernel and pass it to us. We do the down conversion and adjust the length
 * so the caller knows what to write as the returned length into the target when
 * it copies the down converted values into the target.
 *
 * For normal integral types, we just need to byte swap. No size changes.
 *
 * For strings and node data, there's no conversion needed.
 *
 * For opaque data, per sysctl OID converts take care of it.
 */
static void h2g_old_sysctl(void *holdp, size_t *holdlen, uint32_t kind)
{
    size_t len;
    int hlen, glen;
    uint8_t *hp, *gp;

    /*
     * Although rare, we can have arrays of sysctl. Both sysctl_old_ddb in
     * kern_sysctl.c and show_var in sbin/sysctl/sysctl.c have code that loops
     * this way.  *holdlen has been set by the kernel to the host's length.
     * Only LONG and ULONG on ABI32 have different sizes: see below.
     */
    gp = hp = (uint8_t *)holdp;
    len = 0;
    hlen = host_ctl_size[kind & CTLTYPE];
    glen = guest_ctl_size[kind & CTLTYPE];

    /*
     * hlen == 0 for CTLTYPE_STRING and CTLTYPE_NODE, which need no conversion
     * as well as CTLTYPE_OPAQUE, which needs special converters.
     */
    if (hlen == 0) {
        return;
    }

    while (len < *holdlen) {
        if (hlen == glen) {
            switch (hlen) {
            case 1:
                /* Nothing needed: no byteswapping and assigning in place */
                break;
            case 2:
                *(uint16_t *)gp = tswap16(*(uint16_t *)hp);
                break;
            case 4:
                *(uint32_t *)gp = tswap32(*(uint32_t *)hp);
                break;
            case 8:
                *(uint64_t *)gp = tswap64(*(uint64_t *)hp);
                break;
            default:
                g_assert_not_reached();
            }
        } else {
#ifdef TARGET_ABI32
            /*
             * Saturating assignment for the only two types that differ between
             * 32-bit and 64-bit machines. All other integral types have the
             * same, fixed size and will be converted w/o loss of precision
             * in the above switch.
             */
            switch (kind & CTLTYPE) {
            case CTLTYPE_LONG:
                *(abi_long *)gp = tswap32(h2g_long_sat(*(long *)hp));
                break;
            case CTLTYPE_ULONG:
                *(abi_ulong *)gp = tswap32(h2g_ulong_sat(*(u_long *)hp));
                break;
            default:
                g_assert_not_reached();
            }
#else
            g_assert_not_reached();
#endif
        }
        gp += glen;
        hp += hlen;
        len += hlen;
    }
#ifdef TARGET_ABI32
    if (hlen != glen) {
        *holdlen = (*holdlen / hlen) * glen;
    }
#endif
}

/*
 * Convert the undocmented name2oid sysctl data for the target.
 */
static inline void sysctl_name2oid(uint32_t *holdp, size_t holdlen)
{
    size_t i, num = holdlen / sizeof(uint32_t);

    for (i = 0; i < num; i++) {
        holdp[i] = tswap32(holdp[i]);
    }
}

static inline void sysctl_oidfmt(uint32_t *holdp)
{
    /* byte swap the kind */
    holdp[0] = tswap32(holdp[0]);
}

static abi_long do_freebsd_sysctl_oid(CPUArchState *env, int32_t *snamep,
        int32_t namelen, void *holdp, size_t *holdlenp, void *hnewp,
        size_t newlen)
{
    uint32_t kind = 0;
    abi_long ret;
    size_t holdlen, oldlen;
#ifdef TARGET_ABI32
    void *old_holdp;
#endif

    holdlen = oldlen = *holdlenp;
    oidfmt(snamep, namelen, NULL, &kind);

    /* Handle some arch/emulator dependent sysctl()'s here. */
    switch (snamep[0]) {
    case CTL_KERN:
        switch (snamep[1]) {
        case KERN_USRSTACK:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_USRSTACK);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        case KERN_PS_STRINGS:
            if (oldlen) {
                (*(abi_ulong *)holdp) = tswapal(TARGET_PS_STRINGS);
            }
            holdlen = sizeof(abi_ulong);
            ret = 0;
            goto out;

        case KERN_PROC:
            switch (snamep[2]) {
            case KERN_PROC_ALL:
            case KERN_PROC_PROC:
            case KERN_PROC_PID:
            case KERN_PROC_PID | KERN_PROC_INC_THREAD:
            case KERN_PROC_PGRP:
            case KERN_PROC_PGRP | KERN_PROC_INC_THREAD:
            case KERN_PROC_SESSION:
            case KERN_PROC_SESSION | KERN_PROC_INC_THREAD:
            case KERN_PROC_TTY:
            case KERN_PROC_TTY | KERN_PROC_INC_THREAD:
            case KERN_PROC_UID:
            case KERN_PROC_UID | KERN_PROC_INC_THREAD:
            case KERN_PROC_RUID:
            case KERN_PROC_RUID | KERN_PROC_INC_THREAD:
                ret = do_sysctl_kern_getprocs(snamep[2], snamep[3], oldlen,
                                              holdp, &holdlen);
                goto out;

            case KERN_PROC_FILEDESC:
                ret = do_sysctl_kern_proc_filedesc(snamep[3], oldlen, holdp,
                                                   &holdlen);
                goto out;

            case KERN_PROC_VMMAP:
                ret = do_sysctl_kern_proc_vmmap(snamep[3], oldlen, holdp,
                                                &holdlen);
                goto out;

            default:
                break;
            }
            break;

        default:
            break;
        }
        break;

    case CTL_HW:
        switch (snamep[1]) {
        case HW_MACHINE:
            holdlen = sizeof(TARGET_HW_MACHINE);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE, oldlen);
            }
            ret = 0;
            goto out;

        case HW_MACHINE_ARCH:
        {
            holdlen = sizeof(TARGET_HW_MACHINE_ARCH);
            if (holdp) {
                strlcpy(holdp, TARGET_HW_MACHINE_ARCH, oldlen);
            }
            ret = 0;
            goto out;
        }
        case HW_NCPU:
            if (oldlen) {
                (*(abi_int *)holdp) = tswap32(bsd_get_ncpu());
            }
            holdlen = sizeof(int32_t);
            ret = 0;
            goto out;
#if defined(TARGET_ARM)
        case HW_FLOATINGPT:
            if (oldlen) {
                ARMCPU *cpu = env_archcpu(env);
                *(abi_int *)holdp = cpu_isar_feature(aa32_vfp, cpu);
            }
            holdlen = sizeof(abi_int);
            ret = 0;
            goto out;
#endif


#ifdef TARGET_ABI32
        case HW_PHYSMEM:
        case HW_USERMEM:
        case HW_REALMEM:
            holdlen = sizeof(abi_ulong);
            ret = 0;

            if (oldlen) {
                int mib[2] = {snamep[0], snamep[1]};
                unsigned long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctl(mib, 2, &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    lvalue = cap_memory(lvalue);
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                }
            }
            goto out;
#endif

        default:
        {
            static int oid_hw_availpages;
            static int oid_hw_pagesizes;

            if (!oid_hw_availpages) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.availpages", real_oid, &len) >= 0) {
                    oid_hw_availpages = real_oid[1];
                }
            }
            if (!oid_hw_pagesizes) {
                int real_oid[CTL_MAXNAME + 2];
                size_t len = sizeof(real_oid) / sizeof(int);

                if (sysctlnametomib("hw.pagesizes", real_oid, &len) >= 0) {
                    oid_hw_pagesizes = real_oid[1];
                }
            }

            if (oid_hw_availpages && snamep[1] == oid_hw_availpages) {
                long lvalue;
                size_t len = sizeof(lvalue);

                if (sysctlbyname("hw.availpages", &lvalue, &len, NULL, 0) == -1) {
                    ret = -1;
                } else {
                    if (oldlen) {
                        lvalue = scale_to_guest_pages(lvalue);
                        (*(abi_ulong *)holdp) = tswapal((abi_ulong)lvalue);
                    }
                    holdlen = sizeof(abi_ulong);
                    ret = 0;
                }
                goto out;
            }

            if (oid_hw_pagesizes && snamep[1] == oid_hw_pagesizes) {
                if (oldlen) {
                    (*(abi_ulong *)holdp) = tswapal((abi_ulong)TARGET_PAGE_SIZE);
                    ((abi_ulong *)holdp)[1] = 0;
                }
                holdlen = sizeof(abi_ulong) * 2;
                ret = 0;
                goto out;
            }
            break;
        }
        }
        break;

    default:
        break;
    }

#ifdef TARGET_ABI32
    /*
     * For long and ulong with a 64-bit host and a 32-bit target we have to do
     * special things. holdlen here is the length provided by the target to the
     * system call. So we allocate a buffer twice as large because longs are
     * twice as big on the host which will be writing them. In h2g_old_sysctl
     * we'll adjust them and adjust the length.
     */
    if (kind == CTLTYPE_LONG || kind == CTLTYPE_ULONG) {
        old_holdp = holdp;
        holdlen = holdlen * 2;
        holdp = g_malloc(holdlen);
    }
#endif

    ret = get_errno(sysctl(snamep, namelen, holdp, &holdlen, hnewp, newlen));
    if (!ret && (holdp != 0)) {

        if (snamep[0] == CTL_SYSCTL) {
            switch (snamep[1]) {
            case CTL_SYSCTL_NEXT:
            case CTL_SYSCTL_NAME2OID:
            case CTL_SYSCTL_NEXTNOSKIP:
                /*
                 * All of these return an OID array, so we need to convert to
                 * target.
                 */
                sysctl_name2oid(holdp, holdlen);
                break;

            case CTL_SYSCTL_OIDFMT:
                /* Handle oidfmt */
                sysctl_oidfmt(holdp);
                break;
            case CTL_SYSCTL_OIDDESCR:
            case CTL_SYSCTL_OIDLABEL:
            default:
                /* Handle it based on the type */
                h2g_old_sysctl(holdp, &holdlen, kind);
                /* NB: None of these are LONG or ULONG */
                break;
            }
        } else {
            /*
             * Need to convert from host to target. All the weird special cases
             * are handled above.
             */
            h2g_old_sysctl(holdp, &holdlen, kind);
#ifdef TARGET_ABI32
            /*
             * For the 32-bit on 64-bit case, for longs we need to copy the
             * now-converted buffer to the target and free the buffer.
             */
            if (kind == CTLTYPE_LONG || kind == CTLTYPE_ULONG) {
                memcpy(old_holdp, holdp, holdlen);
                g_free(holdp);
                holdp = old_holdp;
            }
#endif
        }
    }

out:
    *holdlenp = holdlen;
    return ret;
}

/*
 * This syscall was created to make sysctlbyname(3) more efficient, but we can't
 * really provide it in bsd-user.  Notably, we must always translate the names
 * independently since some sysctl values have to be faked for the target
 * environment, so it still has to break down to two syscalls for the underlying
 * implementation.
 */
abi_long do_freebsd_sysctlbyname(CPUArchState *env, abi_ulong namep,
        int32_t namelen, abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp,
        abi_ulong newlen)
{
    abi_long ret = -TARGET_EFAULT;
    void *holdp = NULL, *hnewp = NULL;
    char *snamep = NULL;
    int oid[CTL_MAXNAME + 2];
    size_t holdlen, oidplen;
    abi_ulong oldlen = 0;

    /* oldlenp is read/write, pre-check here for write */
    if (oldlenp) {
        if (!access_ok(VERIFY_WRITE, oldlenp, sizeof(abi_ulong)) ||
            get_user_ual(oldlen, oldlenp)) {
            goto out;
        }
    }
    snamep = lock_user_string(namep);
    if (snamep == NULL) {
        goto out;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            goto out;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            goto out;
        }
    }
    holdlen = oldlen;

    oidplen = ARRAY_SIZE(oid);
    if (sysctlnametomib(snamep, oid, &oidplen) != 0) {
        ret = -TARGET_EINVAL;
        goto out;
    }

    ret = do_freebsd_sysctl_oid(env, oid, oidplen, holdp, &holdlen, hnewp,
        newlen);

    /*
     * writeability pre-checked above. __sysctl(2) returns ENOMEM and updates
     * oldlenp for the proper size to use.
     */
    if (oldlenp && (ret == 0 || ret == -TARGET_ENOMEM)) {
        put_user_ual(holdlen, oldlenp);
    }
out:
    unlock_user(snamep, namep, 0);
    unlock_user(holdp, oldp, ret == 0 ? holdlen : 0);
    unlock_user(hnewp, newp, 0);

    return ret;
}

abi_long do_freebsd_sysctl(CPUArchState *env, abi_ulong namep, int32_t namelen,
        abi_ulong oldp, abi_ulong oldlenp, abi_ulong newp, abi_ulong newlen)
{
    abi_long ret = -TARGET_EFAULT;
    void *hnamep, *holdp = NULL, *hnewp = NULL;
    size_t holdlen;
    abi_ulong oldlen = 0;
    int32_t *snamep = g_malloc(sizeof(int32_t) * namelen), *p, *q, i;

    /* oldlenp is read/write, pre-check here for write */
    if (oldlenp) {
        if (!access_ok(VERIFY_WRITE, oldlenp, sizeof(abi_ulong)) ||
            get_user_ual(oldlen, oldlenp)) {
            goto out;
        }
    }
    hnamep = lock_user(VERIFY_READ, namep, namelen, 1);
    if (hnamep == NULL) {
        goto out;
    }
    if (newp) {
        hnewp = lock_user(VERIFY_READ, newp, newlen, 1);
        if (hnewp == NULL) {
            goto out;
        }
    }
    if (oldp) {
        holdp = lock_user(VERIFY_WRITE, oldp, oldlen, 0);
        if (holdp == NULL) {
            goto out;
        }
    }
    holdlen = oldlen;
    for (p = hnamep, q = snamep, i = 0; i < namelen; p++, i++, q++) {
        *q = tswap32(*p);
    }

    ret = do_freebsd_sysctl_oid(env, snamep, namelen, holdp, &holdlen, hnewp,
        newlen);

    /*
     * writeability pre-checked above. __sysctl(2) returns ENOMEM and updates
     * oldlenp for the proper size to use.
     */
    if (oldlenp && (ret == 0 || ret == -TARGET_ENOMEM)) {
        put_user_ual(holdlen, oldlenp);
    }
    unlock_user(hnamep, namep, 0);
    unlock_user(holdp, oldp, ret == 0 ? holdlen : 0);
out:
    g_free(snamep);
    return ret;
}

/* sysarch() is architecture dependent. */
abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2)
{
    return do_freebsd_arch_sysarch(cpu_env, arg1, arg2);
}
