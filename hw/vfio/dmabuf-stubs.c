/*
 * Copyright (c) 2026 Intel and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "hw/vfio/vfio-device.h"

int vfio_device_create_dmabuf_fd(struct iovec *iov, unsigned int iov_cnt,
                                 Error **errp)
{
    error_setg(errp, "VFIO dmabuf support is not enabled");
    return VFIO_DMABUF_CREATE_HOST_ERROR;
}
