#ifndef VFIO_USER_DEVICE_H
#define VFIO_USER_DEVICE_H

/*
 * vfio protocol over a UNIX socket device handling.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "linux/vfio.h"

#include "hw/vfio-user/proxy.h"

int vfio_user_get_device_info(VFIOUserProxy *proxy,
                              struct vfio_device_info *info);

#endif /* VFIO_USER_DEVICE_H */
