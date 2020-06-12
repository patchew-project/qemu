/*
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

#ifndef FD_TRANS_H
#define FD_TRANS_H

#include "qemu.h"
#include "fd-trans-type.h"

/*
 * Return a duplicate of the given fd_trans_table. This function always
 * succeeds. Ownership of the pointed-to table is yielded to the caller. The
 * caller is responsible for freeing the table when it is no longer in-use.
 */
struct fd_trans_table *fd_trans_table_clone(struct fd_trans_table *tbl);

static inline TargetFdDataFunc fd_trans_target_to_host_data(int fd)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct fd_trans_table *tbl = ts->fd_trans_tbl;

    if (fd >= 0 && fd < tbl->fd_max && tbl->entries[fd]) {
        return tbl->entries[fd]->target_to_host_data;
    }
    return NULL;
}

static inline TargetFdDataFunc fd_trans_host_to_target_data(int fd)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct fd_trans_table *tbl = ts->fd_trans_tbl;

    if (fd >= 0 && fd < tbl->fd_max && tbl->entries[fd]) {
        return tbl->entries[fd]->host_to_target_data;
    }
    return NULL;
}

static inline TargetFdAddrFunc fd_trans_target_to_host_addr(int fd)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct fd_trans_table *tbl = ts->fd_trans_tbl;

    if (fd >= 0 && fd < tbl->fd_max && tbl->entries[fd]) {
        return tbl->entries[fd]->target_to_host_addr;
    }
    return NULL;
}

static inline void fd_trans_register(int fd, TargetFdTrans *trans)
{
    unsigned int oldmax;

    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct fd_trans_table *tbl = ts->fd_trans_tbl;

    /*
     * TODO: This is racy. Updates to tbl->entries should be guarded by
     * a lock.
     */
    if (fd >= tbl->fd_max) {
        oldmax = tbl->fd_max;
        tbl->fd_max = ((fd >> 6) + 1) << 6; /* by slice of 64 entries */
        tbl->entries = g_renew(TargetFdTrans *, tbl->entries, tbl->fd_max);
        memset((void *)(tbl->entries + oldmax), 0,
               (tbl->fd_max - oldmax) * sizeof(TargetFdTrans *));
    }
    tbl->entries[fd] = trans;
}

static inline void fd_trans_unregister(int fd)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct fd_trans_table *tbl = ts->fd_trans_tbl;

    if (fd >= 0 && fd < tbl->fd_max) {
        tbl->entries[fd] = NULL;
    }
}

static inline void fd_trans_dup(int oldfd, int newfd)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct fd_trans_table *tbl = ts->fd_trans_tbl;

    fd_trans_unregister(newfd);
    if (oldfd >= 0 && oldfd < tbl->fd_max && tbl->entries[oldfd]) {
        fd_trans_register(newfd, tbl->entries[oldfd]);
    }
}

extern TargetFdTrans target_packet_trans;
#ifdef CONFIG_RTNETLINK
extern TargetFdTrans target_netlink_route_trans;
#endif
extern TargetFdTrans target_netlink_audit_trans;
extern TargetFdTrans target_signalfd_trans;
extern TargetFdTrans target_eventfd_trans;
#if (defined(TARGET_NR_inotify_init) && defined(__NR_inotify_init)) || \
    (defined(CONFIG_INOTIFY1) && defined(TARGET_NR_inotify_init1) && \
     defined(__NR_inotify_init1))
extern TargetFdTrans target_inotify_trans;
#endif
#endif
