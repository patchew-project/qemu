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
    *obtained = false;
    return true;
}
#endif /* HAVE_GETIFADDRS */
