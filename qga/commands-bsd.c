/*
 * QEMU Guest Agent BSD-specific command implementations
 *
 * Copyright (c) Virtuozzo International GmbH.
 *
 * Authors:
 *  Alexander Ivanov  <alexander.ivanov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qga-qapi-commands.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "commands-common.h"
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <net/if_dl.h>
#include <net/ethernet.h>
#include <paths.h>

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
bool build_fs_mount_list(FsMountList *mounts, Error **errp)
{
    FsMount *mount;
    struct statfs *mntbuf, *mntp;
    struct stat statbuf;
    int i, count, ret;

    count = getmntinfo(&mntbuf, MNT_NOWAIT);
    if (count == 0) {
        error_setg_errno(errp, errno, "getmntinfo failed");
        return false;
    }

    for (i = 0; i < count; i++) {
        mntp = &mntbuf[i];
        ret = stat(mntp->f_mntonname, &statbuf);
        if (ret != 0) {
            error_setg_errno(errp, errno, "stat failed on %s",
                             mntp->f_mntonname);
            return false;
        }

        mount = g_new0(FsMount, 1);

        mount->dirname = g_strdup(mntp->f_mntonname);
        mount->devtype = g_strdup(mntp->f_fstypename);
        mount->devmajor = major(mount->dev);
        mount->devminor = minor(mount->dev);
        mount->fsid = mntp->f_fsid;
        mount->dev = statbuf.st_dev;

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }
    return true;
}
#endif /* CONFIG_FSFREEZE || CONFIG_FSTRIM */

#if defined(CONFIG_FSFREEZE)
static int ufssuspend_fd = -1;
static int ufssuspend_cnt;

int64_t qmp_guest_fsfreeze_do_freeze_list(bool has_mountpoints,
                                          strList *mountpoints,
                                          FsMountList mounts,
                                          Error **errp)
{
    int ret;
    strList *list;
    struct FsMount *mount;

    if (ufssuspend_fd != -1) {
        error_setg(errp, "filesystems have already frozen");
        return -1;
    }

    ufssuspend_cnt = 0;
    ufssuspend_fd = qemu_open(_PATH_UFSSUSPEND, O_RDWR, errp);
    if (ufssuspend_fd == -1) {
        return -1;
    }

    QTAILQ_FOREACH_REVERSE(mount, &mounts, next) {
        /*
         * To issue fsfreeze in the reverse order of mounts, check if the
         * mount is listed in the list here
         */
        if (has_mountpoints) {
            for (list = mountpoints; list; list = list->next) {
                if (g_str_equal(list->value, mount->dirname)) {
                    break;
                }
            }
            if (!list) {
                continue;
            }
        }

        /* Only UFS supports suspend */
        if (!g_str_equal(mount->devtype, "ufs")) {
            continue;
        }

        ret = ioctl(ufssuspend_fd, UFSSUSPEND, &mount->fsid);
        if (ret == -1) {
            /*
             * ioctl returns EBUSY for all the FS except the first one
             * that was suspended
             */
            if (errno == EBUSY) {
                continue;
            }
            error_setg_errno(errp, errno, "failed to freeze %s",
                             mount->dirname);
            goto error;
        }
        ufssuspend_cnt++;
    }
    return ufssuspend_cnt;
error:
    close(ufssuspend_fd);
    ufssuspend_fd = -1;
    return -1;

}

/*
 * We don't need to call UFSRESUME ioctl because all the frozen FS
 * are thawed on /dev/ufssuspend closing.
 */
int qmp_guest_fsfreeze_do_thaw(Error **errp)
{
    int ret = ufssuspend_cnt;
    ufssuspend_cnt = 0;
    if (ufssuspend_fd != -1) {
        close(ufssuspend_fd);
        ufssuspend_fd = -1;
    }
    return ret;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestCpuStatsList *qmp_guest_get_cpustats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
#endif /* CONFIG_FSFREEZE */

#if defined(CONFIG_FSTRIM)
typedef struct FsPool {
    char *name;
    QTAILQ_ENTRY(FsPool) next;
} FsPool;

typedef QTAILQ_HEAD(FsPoolList, FsPool) FsPoolList;

static void build_fs_pool_list(FsPoolList *pools, Error **errp)
{
    FILE *fp;
    char *line = NULL, *p;
    size_t linecap = 0;
    ssize_t linelen;
    FsPool *pool;

    fp = popen("/sbin/zpool list -H", "r");
    if (fp == NULL) {
        error_setg_errno(errp, errno, "failed to run zpool");
        return;
    }

    while ((linelen = getline(&line, &linecap, fp)) > 0) {
        p = strchr(line, '\t');
        if (!p) {
            continue;
        }

        *p = '\0';

        pool = g_new0(FsPool, 1);
        pool->name = g_strdup(line);
        QTAILQ_INSERT_TAIL(pools, pool, next);
    }

    free(line);
    pclose(fp);
}

static void free_fs_pool_list(FsPoolList *pools)
{
    FsPool *pool, *temp;

    if (!pools) {
        return;
    }

    QTAILQ_FOREACH_SAFE(pool, pools, next, temp) {
        QTAILQ_REMOVE(pools, pool, next);
        g_free(pool->name);
        g_free(pool);
    }
}

/*
 * Walk the list of ZFS pools in the guest, and trim them.
 */
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    GuestFilesystemTrimResponse *response;
    GuestFilesystemTrimResultList *list;
    GuestFilesystemTrimResult *result;
    int ret;
    FsPoolList pools;
    FsPool *pool;
    char cmd[256];
    Error *local_err = NULL;

    slog("guest-fstrim called");

    QTAILQ_INIT(&pools);
    build_fs_pool_list(&pools, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    response = g_malloc0(sizeof(*response));

    QTAILQ_FOREACH(pool, &pools, next) {
        result = g_malloc0(sizeof(*result));
        result->path = g_strdup(pool->name);

        list = g_malloc0(sizeof(*list));
        list->value = result;
        list->next = response->paths;
        response->paths = list;

        snprintf(cmd, sizeof(cmd), "/sbin/zpool trim %s", pool->name);
        ret = system(cmd);
        if (ret != 0) {
            result->error = g_strdup_printf("failed to trim %s: %s",
                                            pool->name, strerror(errno));
            result->has_error = true;
            continue;
        }

        result->has_minimum = true;
        result->minimum = 0;
        result->has_trimmed = true;
        result->trimmed = 0;
    }

    free_fs_pool_list(&pools);
    return response;
}
#endif /* CONFIG_FSTRIM */

#ifdef HAVE_GETIFADDRS
/*
 * Fill "buf" with MAC address by ifaddrs. Pointer buf must point to a
 * buffer with ETHER_ADDR_LEN length at least.
 *
 * Returns false in case of an error, otherwise true. "obtained" arguument
 * is true if a MAC address was obtained successful, otherwise false.
 */
bool guest_get_hw_addr(struct ifaddrs *ifa, unsigned char *buf,
                       bool *obtained, Error **errp)
{
    struct sockaddr_dl *sdp;

    *obtained = false;

    if (ifa->ifa_addr->sa_family != AF_LINK) {
        /* We can get HW address only for AF_LINK family. */
        g_debug("failed to get MAC address of %s", ifa->ifa_name);
        return true;
    }

    sdp = (struct sockaddr_dl *)ifa->ifa_addr;
    memcpy(buf, sdp->sdl_data + sdp->sdl_nlen, ETHER_ADDR_LEN);
    *obtained = true;

    return true;
}
#endif /* HAVE_GETIFADDRS */
