/*
 * Copyright (c) 2026 Intel and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"

bool vfio_device_create_dmabuf(struct iovec *iov, unsigned int iov_cnt,
                               int *fd, Error **errp)
{
    error_setg(errp, "VFIO dmabuf support is not enabled");
    return false;
}

bool vfio_device_mmap_dmabuf(struct iovec *iov, unsigned int iov_cnt,
                             void **addr, size_t total_size, Error **errp)
{
    error_setg(errp, "VFIO mmap dmabuf support is not enabled");
    return false;
}
