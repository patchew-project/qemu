/*
 * Copyright (C) 2023 Intel Corporation.
 * Copyright (c) 2019, Mellanox Technologies. All rights reserved.
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Copied from
 * https://github.com/linux-rdma/rdma-core/blob/master/util/open_cdev.c
 *
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "qemu/osdep.h"
#include "qemu/char_dev.h"

static int open_cdev_internal(const char *path, dev_t cdev)
{
    struct stat st;
    int fd;

    fd = qemu_open_old(path, O_RDWR);
    if (fd == -1) {
        return -1;
    }
    if (fstat(fd, &st) || !S_ISCHR(st.st_mode) ||
        (cdev != 0 && st.st_rdev != cdev)) {
        close(fd);
        return -1;
    }
    return fd;
}

static int open_cdev_robust(dev_t cdev)
{
    char *devpath;
    int ret;

    /*
     * This assumes that udev is being used and is creating the /dev/char/
     * symlinks.
     */
    devpath = g_strdup_printf("/dev/char/%u:%u", major(cdev), minor(cdev));
    ret = open_cdev_internal(devpath, cdev);
    g_free(devpath);
    return ret;
}

int open_cdev(const char *devpath, dev_t cdev)
{
    int fd;

    fd = open_cdev_internal(devpath, cdev);
    if (fd == -1 && cdev != 0) {
        return open_cdev_robust(cdev);
    }
    return fd;
}
