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

static int vfio_user_device_io_get_irq_info(VFIODevice *vbasedev,
                                            struct vfio_irq_info *info)
{
    VFIOUserProxy *proxy = vbasedev->proxy;
    VFIOUserIRQInfo msg;

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_IRQ_INFO,
                          sizeof(msg), 0);
    msg.argsz = info->argsz;
    msg.index = info->index;

    vfio_user_send_wait(proxy, &msg.hdr, NULL, 0);
    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }
    trace_vfio_user_get_irq_info(msg.index, msg.flags, msg.count);

    memcpy(info, &msg.argsz, sizeof(*info));
    return 0;
}

static int irq_howmany(int *fdp, uint32_t cur, uint32_t max)
{
    int n = 0;

    if (fdp[cur] != -1) {
        do {
            n++;
        } while (n < max && fdp[cur + n] != -1);
    } else {
        do {
            n++;
        } while (n < max && fdp[cur + n] == -1);
    }

    return n;
}

static int vfio_user_device_io_set_irqs(VFIODevice *vbasedev,
                                        struct vfio_irq_set *irq)
{
    VFIOUserProxy *proxy = vbasedev->proxy;
    g_autofree VFIOUserIRQSet *msgp = NULL;
    uint32_t size, nfds, send_fds, sent_fds, max;

    if (irq->argsz < sizeof(*irq)) {
        error_printf("vfio_user_set_irqs argsz too small\n");
        return -EINVAL;
    }

    /*
     * Handle simple case
     */
    if ((irq->flags & VFIO_IRQ_SET_DATA_EVENTFD) == 0) {
        size = sizeof(VFIOUserHdr) + irq->argsz;
        msgp = g_malloc0(size);

        vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_SET_IRQS, size, 0);
        msgp->argsz = irq->argsz;
        msgp->flags = irq->flags;
        msgp->index = irq->index;
        msgp->start = irq->start;
        msgp->count = irq->count;
        trace_vfio_user_set_irqs(msgp->index, msgp->start, msgp->count,
                                 msgp->flags);

        vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0);
        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            return -msgp->hdr.error_reply;
        }

        return 0;
    }

    /*
     * Calculate the number of FDs to send
     * and adjust argsz
     */
    nfds = (irq->argsz - sizeof(*irq)) / sizeof(int);
    irq->argsz = sizeof(*irq);
    msgp = g_malloc0(sizeof(*msgp));
    /*
     * Send in chunks if over max_send_fds
     */
    for (sent_fds = 0; nfds > sent_fds; sent_fds += send_fds) {
        VFIOUserFDs *arg_fds, loop_fds;

        /* must send all valid FDs or all invalid FDs in single msg */
        max = nfds - sent_fds;
        if (max > proxy->max_send_fds) {
            max = proxy->max_send_fds;
        }
        send_fds = irq_howmany((int *)irq->data, sent_fds, max);

        vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_SET_IRQS,
                              sizeof(*msgp), 0);
        msgp->argsz = irq->argsz;
        msgp->flags = irq->flags;
        msgp->index = irq->index;
        msgp->start = irq->start + sent_fds;
        msgp->count = send_fds;
        trace_vfio_user_set_irqs(msgp->index, msgp->start, msgp->count,
                                 msgp->flags);

        loop_fds.send_fds = send_fds;
        loop_fds.recv_fds = 0;
        loop_fds.fds = (int *)irq->data + sent_fds;
        arg_fds = loop_fds.fds[0] != -1 ? &loop_fds : NULL;

        vfio_user_send_wait(proxy, &msgp->hdr, arg_fds, 0);
        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            return -msgp->hdr.error_reply;
        }
    }

    return 0;
}

static int vfio_user_device_io_region_read(VFIODevice *vbasedev, uint8_t index,
                                           off_t off, uint32_t count,
                                           void *data)
{
    g_autofree VFIOUserRegionRW *msgp = NULL;
    VFIOUserProxy *proxy = vbasedev->proxy;
    int size = sizeof(*msgp) + count;

    if (count > proxy->max_xfer_size) {
        return -EINVAL;
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_READ, sizeof(*msgp), 0);
    msgp->offset = off;
    msgp->region = index;
    msgp->count = count;
    trace_vfio_user_region_rw(msgp->region, msgp->offset, msgp->count);

    vfio_user_send_wait(proxy, &msgp->hdr, NULL, size);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    } else if (msgp->count > count) {
        return -E2BIG;
    } else {
        memcpy(data, &msgp->data, msgp->count);
    }

    return msgp->count;
}

static int vfio_user_device_io_region_write(VFIODevice *vbasedev, uint8_t index,
                                            off_t off, unsigned count,
                                            void *data, bool post)
{
    VFIOUserRegionRW *msgp = NULL;
    VFIOUserProxy *proxy = vbasedev->proxy;
    int size = sizeof(*msgp) + count;
    int ret;

    if (count > proxy->max_xfer_size) {
        return -EINVAL;
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_WRITE, size, 0);
    msgp->offset = off;
    msgp->region = index;
    msgp->count = count;
    memcpy(&msgp->data, data, count);
    trace_vfio_user_region_rw(msgp->region, msgp->offset, msgp->count);

    /* Ignore post: all writes are synchronous/non-posted. */
    vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        ret = -msgp->hdr.error_reply;
    } else {
        ret = count;
    }

    g_free(msgp);
    return ret;
}

/*
 * Socket-based io_ops
 */
VFIODeviceIOOps vfio_user_device_io_ops_sock = {
    .get_region_info = vfio_user_device_io_get_region_info,
    .get_irq_info = vfio_user_device_io_get_irq_info,
    .set_irqs = vfio_user_device_io_set_irqs,
    .region_read = vfio_user_device_io_region_read,
    .region_write = vfio_user_device_io_region_write,

};
