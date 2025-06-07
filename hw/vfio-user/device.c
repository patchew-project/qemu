/*
 * vfio protocol over a UNIX socket device handling.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/vfio-user/device.h"
#include "hw/vfio-user/trace.h"

/*
 * These are to defend against a malign server trying
 * to force us to run out of memory.
 */
#define VFIO_USER_MAX_REGIONS   100
#define VFIO_USER_MAX_IRQS      50

int vfio_user_get_device_info(VFIOUserProxy *proxy,
                              struct vfio_device_info *info)
{
    VFIOUserDeviceInfo msg;
    uint32_t argsz = sizeof(msg) - sizeof(msg.hdr);

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_INFO, sizeof(msg), 0);
    msg.argsz = argsz;

    vfio_user_send_wait(proxy, &msg.hdr, NULL, 0);
    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }
    trace_vfio_user_get_info(msg.num_regions, msg.num_irqs);

    memcpy(info, &msg.argsz, argsz);

    /* defend against a malicious server */
    if (info->num_regions > VFIO_USER_MAX_REGIONS ||
        info->num_irqs > VFIO_USER_MAX_IRQS) {
        error_printf("%s: invalid reply\n", __func__);
        return -EINVAL;
    }

    return 0;
}

static int vfio_user_get_region_info(VFIOUserProxy *proxy,
                                     struct vfio_region_info *info,
                                     VFIOUserFDs *fds)
{
    g_autofree VFIOUserRegionInfo *msgp = NULL;
    uint32_t size;

    /* data returned can be larger than vfio_region_info */
    if (info->argsz < sizeof(*info)) {
        error_printf("vfio_user_get_region_info argsz too small\n");
        return -E2BIG;
    }
    if (fds != NULL && fds->send_fds != 0) {
        error_printf("vfio_user_get_region_info can't send FDs\n");
        return -EINVAL;
    }

    size = info->argsz + sizeof(VFIOUserHdr);
    msgp = g_malloc0(size);

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_GET_REGION_INFO,
                          sizeof(*msgp), 0);
    msgp->argsz = info->argsz;
    msgp->index = info->index;

    vfio_user_send_wait(proxy, &msgp->hdr, fds, size);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    }
    trace_vfio_user_get_region_info(msgp->index, msgp->flags, msgp->size);

    memcpy(info, &msgp->argsz, info->argsz);
    return 0;
}


static int vfio_user_device_io_get_region_info(VFIODevice *vbasedev,
                                               struct vfio_region_info *info,
                                               int *fd)
{
    VFIOUserFDs fds = { 0, 1, fd};
    int ret;

    if (info->index > vbasedev->num_regions) {
        return -EINVAL;
    }

    ret = vfio_user_get_region_info(vbasedev->proxy, info, &fds);
    if (ret) {
        return ret;
    }

    /* cap_offset in valid area */
    if ((info->flags & VFIO_REGION_INFO_FLAG_CAPS) &&
        (info->cap_offset < sizeof(*info) || info->cap_offset > info->argsz)) {
        return -EINVAL;
    }

    return 0;
}

/*
 * Socket-based io_ops
 */
VFIODeviceIOOps vfio_user_device_io_ops_sock = {
    .get_region_info = vfio_user_device_io_get_region_info,
};
