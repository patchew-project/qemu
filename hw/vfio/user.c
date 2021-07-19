/*
 * vfio protocol over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "hw/hw.h"
#include "hw/vfio/vfio-common.h"
#include "hw/vfio/vfio.h"
#include "qemu/sockets.h"
#include "io/channel.h"
#include "io/channel-util.h"
#include "sysemu/iothread.h"
#include "user.h"

static uint64_t max_xfer_size = VFIO_USER_DEF_MAX_XFER;
static IOThread *vfio_user_iothread;
static void vfio_user_send_locked(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                                  VFIOUserFDs *fds);
static void vfio_user_send(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                           VFIOUserFDs *fds);
static void vfio_user_shutdown(VFIOProxy *proxy);

static void vfio_user_shutdown(VFIOProxy *proxy)
{
    qio_channel_shutdown(proxy->ioc, QIO_CHANNEL_SHUTDOWN_READ, NULL);
    qio_channel_set_aio_fd_handler(proxy->ioc,
                                   iothread_get_aio_context(vfio_user_iothread),
                                   NULL, NULL, NULL);
}

void vfio_user_send_reply(VFIOProxy *proxy, char *buf, int ret)
{
    vfio_user_hdr_t *hdr = (vfio_user_hdr_t *)buf;

    /*
     * convert header to associated reply
     * positive ret is reply size, negative is error code
     */
    hdr->flags = VFIO_USER_REPLY;
    if (ret > 0) {
        hdr->size = ret;
    } else if (ret < 0) {
        hdr->flags |= VFIO_USER_ERROR;
        hdr->error_reply = -ret;
        hdr->size = sizeof(*hdr);
    }
    vfio_user_send(proxy, hdr, NULL);
}

void vfio_user_recv(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOProxy *proxy = vbasedev->proxy;
    VFIOUserReply *reply = NULL;
    g_autofree int *fdp = NULL;
    VFIOUserFDs reqfds = { 0, 0, fdp };
    vfio_user_hdr_t msg;
    struct iovec iov = {
        .iov_base = &msg,
        .iov_len = sizeof(msg),
    };
    int isreply, i, ret;
    size_t msgleft, numfds = 0;
    char *data = NULL;
    g_autofree char *buf = NULL;
    Error *local_err = NULL;

    qemu_mutex_lock(&proxy->lock);
    if (proxy->state == CLOSING) {
        qemu_mutex_unlock(&proxy->lock);
        return;
    }

    ret = qio_channel_readv_full(proxy->ioc, &iov, 1, &fdp, &numfds,
                                 &local_err);
    if (ret <= 0) {
        /* read error or other side closed connection */
        error_setg_errno(&local_err, errno, "vfio_user_recv read error");
        goto fatal;
    }

    if (ret < sizeof(msg)) {
        error_setg(&local_err, "vfio_user_recv short read of header");
        goto err;
    }

    /*
     * For replies, find the matching pending request
     */
    switch (msg.flags & VFIO_USER_TYPE) {
    case VFIO_USER_REQUEST:
        isreply = 0;
        break;
    case VFIO_USER_REPLY:
        isreply = 1;
        break;
    default:
        error_setg(&local_err, "vfio_user_recv unknown message type");
        goto err;
    }

    if (isreply) {
        QTAILQ_FOREACH(reply, &proxy->pending, next) {
            if (msg.id == reply->id) {
                break;
            }
        }
        if (reply == NULL) {
            error_setg(&local_err, "vfio_user_recv unexpected reply");
            goto err;
        }
        QTAILQ_REMOVE(&proxy->pending, reply, next);

        /*
         * Process any received FDs
         */
        if (numfds != 0) {
            if (reply->fds == NULL || reply->fds->recv_fds < numfds) {
                error_setg(&local_err, "vfio_user_recv unexpected FDs");
                goto err;
            }
            reply->fds->recv_fds = numfds;
            memcpy(reply->fds->fds, fdp, numfds * sizeof(int));
        }

    } else {
        /*
         * The client doesn't expect any FDs in requests, but
         * they will be expected on the server
         */
        if (numfds != 0 && (proxy->flags & VFIO_PROXY_CLIENT)) {
            error_setg(&local_err, "vfio_user_recv fd in client reply");
            goto err;
        }
        reqfds.recv_fds = numfds;
    }

    /*
     * put the whole message into a single buffer
     */
    msgleft = msg.size - sizeof(msg);
    if (isreply) {
        if (msg.size > reply->rsize) {
            error_setg(&local_err,
                       "vfio_user_recv reply larger than recv buffer");
            goto fatal;
        }
        *reply->msg = msg;
        data = (char *)reply->msg + sizeof(msg);
    } else {
        if (msg.size > max_xfer_size) {
            error_setg(&local_err, "vfio_user_recv request larger than max");
            goto fatal;
        }
        buf = g_malloc0(msg.size);
        memcpy(buf, &msg, sizeof(msg));
        data = buf + sizeof(msg);
    }

    if (msgleft != 0) {
        ret = qio_channel_read(proxy->ioc, data, msgleft, &local_err);
        if (ret < 0) {
            goto fatal;
        }
        if (ret != msgleft) {
            error_setg(&local_err, "vfio_user_recv short read of msg body");
            goto err;
        }
    }

    /*
     * Replies signal a waiter, requests get processed by vfio code
     * that may assume the iothread lock is held.
     */
    qemu_mutex_unlock(&proxy->lock);
    if (isreply) {
        reply->complete = 1;
        qemu_cond_signal(&reply->cv);
    } else {
        qemu_mutex_lock_iothread();
        /*
         * make sure proxy wasn't closed while we waited
         * checking without holding the proxy lock is safe
         * since state is only set to CLOSING when iolock is held
         */
        if (proxy->state != CLOSING) {
            ret = proxy->request(proxy->reqarg, buf, &reqfds);
            if (ret < 0 && !(msg.flags & VFIO_USER_NO_REPLY)) {
                vfio_user_send_reply(proxy, buf, ret);
            }
        }
        qemu_mutex_unlock_iothread();
    }

    return;
 fatal:
    vfio_user_shutdown(proxy);
    proxy->state = RECV_ERROR;

 err:
    qemu_mutex_unlock(&proxy->lock);
    for (i = 0; i < numfds; i++) {
        close(fdp[i]);
    }
    if (reply != NULL) {
        /* force an error to keep sending thread from hanging */
        reply->msg->flags |= VFIO_USER_ERROR;
        reply->msg->error_reply = EINVAL;
        reply->complete = 1;
        qemu_cond_signal(&reply->cv);
    }
    error_report_err(local_err);
}

static void vfio_user_send_locked(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                                  VFIOUserFDs *fds)
{
    struct iovec iov = {
        .iov_base = msg,
        .iov_len = msg->size,
    };
    size_t numfds = 0;
    int msgleft, ret, *fdp = NULL;
    char *buf;
    Error *local_err = NULL;

    if (proxy->state != CONNECTED) {
        msg->flags |= VFIO_USER_ERROR;
        msg->error_reply = ECONNRESET;
        return;
    }

    if (fds != NULL && fds->send_fds != 0) {
        numfds = fds->send_fds;
        fdp = fds->fds;
    }
    ret = qio_channel_writev_full(proxy->ioc, &iov, 1, fdp, numfds, &local_err);
    if (ret < 0) {
        goto err;
    }
    if (ret == msg->size) {
        return;
    }

    buf = iov.iov_base + ret;
    msgleft = iov.iov_len - ret;
    do {
        ret = qio_channel_write(proxy->ioc, buf, msgleft, &local_err);
        if (ret < 0) {
            goto err;
        }
        buf += ret, msgleft -= ret;
    } while (msgleft != 0);
    return;

 err:
    error_report_err(local_err);
}

static void vfio_user_send(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                           VFIOUserFDs *fds)
{
    bool iolock = qemu_mutex_iothread_locked();

    if (iolock) {
        qemu_mutex_unlock_iothread();
    }
    qemu_mutex_lock(&proxy->lock);
    vfio_user_send_locked(proxy, msg, fds);
    qemu_mutex_unlock(&proxy->lock);
    if (iolock) {
        qemu_mutex_lock_iothread();
    }
}
