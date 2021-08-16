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
#include "io/channel-socket.h"
#include "io/channel-util.h"
#include "sysemu/iothread.h"
#include "user.h"

static uint64_t max_xfer_size = VFIO_USER_DEF_MAX_XFER;
static IOThread *vfio_user_iothread;

static void vfio_user_shutdown(VFIOProxy *proxy);
static void vfio_user_recv(void *opaque);
static void vfio_user_send_locked(VFIOProxy *proxy, VFIOUserHdr *msg,
                                  VFIOUserFDs *fds);
static void vfio_user_send(VFIOProxy *proxy, VFIOUserHdr *msg,
                           VFIOUserFDs *fds);


/*
 * Functions called by main, CPU, or iothread threads
 */

static void vfio_user_shutdown(VFIOProxy *proxy)
{
    qio_channel_shutdown(proxy->ioc, QIO_CHANNEL_SHUTDOWN_READ, NULL);
    qio_channel_set_aio_fd_handler(proxy->ioc,
                                   iothread_get_aio_context(vfio_user_iothread),
                                   NULL, NULL, NULL);
}

static void vfio_user_send_locked(VFIOProxy *proxy, VFIOUserHdr *msg,
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

    if (proxy->state != VFIO_PROXY_CONNECTED) {
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
        buf += ret;
        msgleft -= ret;
    } while (msgleft != 0);
    return;

err:
    msg->flags |= VFIO_USER_ERROR;
    msg->error_reply = EIO;
    error_report_err(local_err);
}

static void vfio_user_send(VFIOProxy *proxy, VFIOUserHdr *msg,
                           VFIOUserFDs *fds)
{

    qemu_mutex_lock(&proxy->lock);
    vfio_user_send_locked(proxy, msg, fds);
    qemu_mutex_unlock(&proxy->lock);
}


/*
 * Functions only called by iothread
 */

void vfio_user_send_reply(VFIOProxy *proxy, char *buf, int ret)
{
    VFIOUserHdr *hdr = (VFIOUserHdr *)buf;

    /*
     * convert header to associated reply
     * positive ret is reply size, negative is error code
     */
    hdr->flags = VFIO_USER_REPLY;
    if (ret >= sizeof(VFIOUserHdr)) {
        hdr->size = ret;
    } else if (ret < 0) {
        hdr->flags |= VFIO_USER_ERROR;
        hdr->error_reply = -ret;
        hdr->size = sizeof(*hdr);
    } else {
        error_printf("vfio_user_send_reply - size too small\n");
        return;
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
    VFIOUserHdr msg;
    struct iovec iov = {
        .iov_base = &msg,
        .iov_len = sizeof(msg),
    };
    bool isreply;
    int i, ret;
    size_t msgleft, numfds = 0;
    char *data = NULL;
    g_autofree char *buf = NULL;
    Error *local_err = NULL;

    qemu_mutex_lock(&proxy->lock);
    if (proxy->state == VFIO_PROXY_CLOSING) {
        qemu_mutex_unlock(&proxy->lock);
        return;
    }

    ret = qio_channel_readv_full(proxy->ioc, &iov, 1, &fdp, &numfds,
                                 &local_err);
    if (ret <= 0) {
        /* read error or other side closed connection */
        goto fatal;
    }

    if (ret < sizeof(msg)) {
        error_setg(&local_err, "vfio_user_recv short read of header");
        goto err;
    }
    if (msg.size < sizeof(VFIOUserHdr)) {
        error_setg(&local_err, "vfio_user_recv bad header size");
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

    msgleft = msg.size - sizeof(msg);
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
    if (isreply) {
        reply->complete = 1;
        if (!reply->nowait) {
            qemu_cond_signal(&reply->cv);
        } else {
            if (msg.flags & VFIO_USER_ERROR) {
                error_printf("vfio_user_rcv error reply on async request ");
                error_printf("command %x error %s\n", msg.command,
                             strerror(msg.error_reply));
            }
            /* just free it if no one is waiting */
            reply->nowait = 0;
            if (proxy->last_nowait == reply) {
                proxy->last_nowait = NULL;
            }
            g_free(reply->msg);
            QTAILQ_INSERT_HEAD(&proxy->free, reply, next);
        }
        qemu_mutex_unlock(&proxy->lock);
    } else {
        qemu_mutex_unlock(&proxy->lock);
        qemu_mutex_lock_iothread();
        /*
         * make sure proxy wasn't closed while we waited
         * checking state without holding the proxy lock is safe
         * since it's only set to CLOSING when BQL is held
         */
        if (proxy->state != VFIO_PROXY_CLOSING) {
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
    proxy->state = VFIO_PROXY_RECV_ERROR;

err:
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
    qemu_mutex_unlock(&proxy->lock);
    error_report_err(local_err);
}

static void vfio_user_cb(void *opaque)
{
    VFIOProxy *proxy = opaque;

    qemu_mutex_lock(&proxy->lock);
    proxy->state = VFIO_PROXY_CLOSED;
    qemu_mutex_unlock(&proxy->lock);
    qemu_cond_signal(&proxy->close_cv);
}


/*
 * Functions called by main or CPU threads
 */

static QLIST_HEAD(, VFIOProxy) vfio_user_sockets =
    QLIST_HEAD_INITIALIZER(vfio_user_sockets);

VFIOProxy *vfio_user_connect_dev(SocketAddress *addr, Error **errp)
{
    VFIOProxy *proxy;
    QIOChannelSocket *sioc;
    QIOChannel *ioc;
    char *sockname;

    if (addr->type != SOCKET_ADDRESS_TYPE_UNIX) {
        error_setg(errp, "vfio_user_connect - bad address family");
        return NULL;
    }
    sockname = addr->u.q_unix.path;

    sioc = qio_channel_socket_new();
    ioc = QIO_CHANNEL(sioc);
    if (qio_channel_socket_connect_sync(sioc, addr, errp)) {
        object_unref(OBJECT(ioc));
        return NULL;
    }
    qio_channel_set_blocking(ioc, true, NULL);

    proxy = g_malloc0(sizeof(VFIOProxy));
    proxy->sockname = sockname;
    proxy->ioc = ioc;
    proxy->flags = VFIO_PROXY_CLIENT;
    proxy->state = VFIO_PROXY_CONNECTED;
    qemu_cond_init(&proxy->close_cv);

    if (vfio_user_iothread == NULL) {
        vfio_user_iothread = iothread_create("VFIO user", errp);
    }

    qemu_mutex_init(&proxy->lock);
    QTAILQ_INIT(&proxy->free);
    QTAILQ_INIT(&proxy->pending);
    QLIST_INSERT_HEAD(&vfio_user_sockets, proxy, next);

    return proxy;
}

void vfio_user_set_reqhandler(VFIODevice *vbasedev,
                              int (*handler)(void *opaque, char *buf,
                                             VFIOUserFDs *fds),
                              void *reqarg)
{
    VFIOProxy *proxy = vbasedev->proxy;

    proxy->request = handler;
    proxy->reqarg = reqarg;
    qio_channel_set_aio_fd_handler(proxy->ioc,
                                   iothread_get_aio_context(vfio_user_iothread),
                                   vfio_user_recv, NULL, vbasedev);
}

void vfio_user_disconnect(VFIOProxy *proxy)
{
    VFIOUserReply *r1, *r2;

    qemu_mutex_lock(&proxy->lock);

    /* our side is quitting */
    if (proxy->state == VFIO_PROXY_CONNECTED) {
        vfio_user_shutdown(proxy);
        if (!QTAILQ_EMPTY(&proxy->pending)) {
            error_printf("vfio_user_disconnect: outstanding requests\n");
        }
    }
    object_unref(OBJECT(proxy->ioc));
    proxy->ioc = NULL;

    proxy->state = VFIO_PROXY_CLOSING;
    QTAILQ_FOREACH_SAFE(r1, &proxy->pending, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->pending, r1, next);
        g_free(r1);
    }
    QTAILQ_FOREACH_SAFE(r1, &proxy->free, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->free, r1, next);
        g_free(r1);
    }

    /*
     * Make sure the iothread isn't blocking anywhere
     * with a ref to this proxy by waiting for a BH
     * handler to run after the proxy fd handlers were
     * deleted above.
     */
    proxy->close_wait = 1;
    aio_bh_schedule_oneshot(iothread_get_aio_context(vfio_user_iothread),
                            vfio_user_cb, proxy);

    /* drop locks so the iothread can make progress */
    qemu_mutex_unlock_iothread();
    qemu_cond_wait(&proxy->close_cv, &proxy->lock);

    /* we now hold the only ref to proxy */
    qemu_mutex_unlock(&proxy->lock);
    qemu_cond_destroy(&proxy->close_cv);
    qemu_mutex_destroy(&proxy->lock);

    qemu_mutex_lock_iothread();

    QLIST_REMOVE(proxy, next);
    if (QLIST_EMPTY(&vfio_user_sockets)) {
        iothread_destroy(vfio_user_iothread);
        vfio_user_iothread = NULL;
    }

    g_free(proxy);
}
