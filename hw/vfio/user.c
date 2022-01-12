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
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qnum.h"
#include "user.h"

static uint64_t max_xfer_size = VFIO_USER_DEF_MAX_XFER;
static uint64_t max_send_fds = VFIO_USER_DEF_MAX_FDS;
static int wait_time = 1000;   /* wait 1 sec for replies */
static IOThread *vfio_user_iothread;

static void vfio_user_shutdown(VFIOProxy *proxy);
static int vfio_user_send_qio(VFIOProxy *proxy, VFIOUserMsg *msg);
static VFIOUserMsg *vfio_user_getmsg(VFIOProxy *proxy, VFIOUserHdr *hdr,
                                     VFIOUserFDs *fds);
static VFIOUserFDs *vfio_user_getfds(int numfds);
static void vfio_user_recycle(VFIOProxy *proxy, VFIOUserMsg *msg);

static void vfio_user_recv(void *opaque);
static int vfio_user_recv_one(VFIOProxy *proxy);
static void vfio_user_send(void *opaque);
static int vfio_user_send_one(VFIOProxy *proxy, VFIOUserMsg *msg);
static void vfio_user_cb(void *opaque);

static void vfio_user_request(void *opaque);
static int vfio_user_send_queued(VFIOProxy *proxy, VFIOUserMsg *msg);
static void vfio_user_send_async(VFIOProxy *proxy, VFIOUserHdr *hdr,
                                 VFIOUserFDs *fds);
static void vfio_user_send_wait(VFIOProxy *proxy, VFIOUserHdr *hdr,
                                VFIOUserFDs *fds, int rsize, bool nobql);
static void vfio_user_request_msg(VFIOUserHdr *hdr, uint16_t cmd,
                                  uint32_t size, uint32_t flags);

static inline void vfio_user_set_error(VFIOUserHdr *hdr, uint32_t err)
{
    hdr->flags |= VFIO_USER_ERROR;
    hdr->error_reply = err;
}

/*
 * Functions called by main, CPU, or iothread threads
 */

static void vfio_user_shutdown(VFIOProxy *proxy)
{
    qio_channel_shutdown(proxy->ioc, QIO_CHANNEL_SHUTDOWN_READ, NULL);
    qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx, NULL, NULL, NULL);
}

static int vfio_user_send_qio(VFIOProxy *proxy, VFIOUserMsg *msg)
{
    VFIOUserFDs *fds =  msg->fds;
    struct iovec iov = {
        .iov_base = msg->hdr,
        .iov_len = msg->hdr->size,
    };
    size_t numfds = 0;
    int ret, *fdp = NULL;
    Error *local_err = NULL;

    if (fds != NULL && fds->send_fds != 0) {
        numfds = fds->send_fds;
        fdp = fds->fds;
    }

    ret = qio_channel_writev_full(proxy->ioc, &iov, 1, fdp, numfds, &local_err);

    if (ret == -1) {
        vfio_user_set_error(msg->hdr, EIO);
        vfio_user_shutdown(proxy);
        error_report_err(local_err);
    }
    return ret;
}

static VFIOUserMsg *vfio_user_getmsg(VFIOProxy *proxy, VFIOUserHdr *hdr,
                                     VFIOUserFDs *fds)
{
    VFIOUserMsg *msg;

    msg = QTAILQ_FIRST(&proxy->free);
    if (msg != NULL) {
        QTAILQ_REMOVE(&proxy->free, msg, next);
    } else {
        msg = g_malloc0(sizeof(*msg));
        qemu_cond_init(&msg->cv);
    }

    msg->hdr = hdr;
    msg->fds = fds;
    return msg;
}

/*
 * Recycle a message list entry to the free list.
 */
static void vfio_user_recycle(VFIOProxy *proxy, VFIOUserMsg *msg)
{
    if (msg->type == VFIO_MSG_NONE) {
        error_printf("vfio_user_recycle - freeing free msg\n");
        return;
    }

    /* free msg buffer if no one is waiting to consume the reply */
    if (msg->type == VFIO_MSG_NOWAIT || msg->type == VFIO_MSG_ASYNC) {
        g_free(msg->hdr);
        if (msg->fds != NULL) {
            g_free(msg->fds);
        }
    }

    msg->type = VFIO_MSG_NONE;
    msg->hdr = NULL;
    msg->fds = NULL;
    msg->complete = false;
    QTAILQ_INSERT_HEAD(&proxy->free, msg, next);
}

static VFIOUserFDs *vfio_user_getfds(int numfds)
{
    VFIOUserFDs *fds = g_malloc0(sizeof(*fds) + (numfds * sizeof(int)));

    fds->fds = (int *)((char *)fds + sizeof(*fds));

    return fds;
}

/*
 * Functions only called by iothread
 */

static void vfio_user_recv(void *opaque)
{
    VFIOProxy *proxy = opaque;

    QEMU_LOCK_GUARD(&proxy->lock);

    if (proxy->state == VFIO_PROXY_CONNECTED) {
        while (vfio_user_recv_one(proxy) == 0) {
            ;
        }
    }
}

/*
 * Receive and process one incoming message.
 *
 * For replies, find matching outgoing request and wake any waiters.
 * For requests, queue in incoming list and run request BH.
 */
static int vfio_user_recv_one(VFIOProxy *proxy)
{
    VFIOUserMsg *msg = NULL;
    g_autofree int *fdp = NULL;
    VFIOUserFDs *reqfds;
    VFIOUserHdr hdr;
    struct iovec iov = {
        .iov_base = &hdr,
        .iov_len = sizeof(hdr),
    };
    bool isreply = false;
    int i, ret;
    size_t msgleft, numfds = 0;
    char *data = NULL;
    char *buf = NULL;
    Error *local_err = NULL;

    /*
     * Read header
     */
    ret = qio_channel_readv_full(proxy->ioc, &iov, 1, &fdp, &numfds,
                                 &local_err);
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        return ret;
    }
    if (ret <= 0) {
        /* read error or other side closed connection */
        if (ret == 0) {
            error_setg(&local_err, "vfio_user_recv server closed socket");
        } else {
            error_prepend(&local_err, "vfio_user_recv");
        }
        goto fatal;
    }
    if (ret < sizeof(msg)) {
        error_setg(&local_err, "vfio_user_recv short read of header");
        goto fatal;
    }

    /*
     * Validate header
     */
    if (hdr.size < sizeof(VFIOUserHdr)) {
        error_setg(&local_err, "vfio_user_recv bad header size");
        goto fatal;
    }
    switch (hdr.flags & VFIO_USER_TYPE) {
    case VFIO_USER_REQUEST:
        isreply = false;
        break;
    case VFIO_USER_REPLY:
        isreply = true;
        break;
    default:
        error_setg(&local_err, "vfio_user_recv unknown message type");
        goto fatal;
    }

    /*
     * For replies, find the matching pending request.
     * For requests, reap incoming FDs.
     */
    if (isreply) {
        QTAILQ_FOREACH(msg, &proxy->pending, next) {
            if (hdr.id == msg->id) {
                break;
            }
        }
        if (msg == NULL) {
            error_setg(&local_err, "vfio_user_recv unexpected reply");
            goto err;
        }
        QTAILQ_REMOVE(&proxy->pending, msg, next);

        /*
         * Process any received FDs
         */
        if (numfds != 0) {
            if (msg->fds == NULL || msg->fds->recv_fds < numfds) {
                error_setg(&local_err, "vfio_user_recv unexpected FDs");
                goto err;
            }
            msg->fds->recv_fds = numfds;
            memcpy(msg->fds->fds, fdp, numfds * sizeof(int));
        }
    } else {
        if (numfds != 0) {
            reqfds = vfio_user_getfds(numfds);
            memcpy(reqfds->fds, fdp, numfds * sizeof(int));
        } else {
            reqfds = NULL;
        }
    }

    /*
     * Put the whole message into a single buffer.
     */
    if (isreply) {
        if (hdr.size > msg->rsize) {
            error_setg(&local_err,
                       "vfio_user_recv reply larger than recv buffer");
            goto err;
        }
        *msg->hdr = hdr;
        data = (char *)msg->hdr + sizeof(hdr);
    } else {
        if (hdr.size > max_xfer_size) {
            error_setg(&local_err, "vfio_user_recv request larger than max");
            goto err;
        }
        buf = g_malloc0(hdr.size);
        memcpy(buf, &hdr, sizeof(hdr));
        data = buf + sizeof(hdr);
        msg = vfio_user_getmsg(proxy, (VFIOUserHdr *)buf, reqfds);
        msg->type = VFIO_MSG_REQ;
    }

    msgleft = hdr.size - sizeof(hdr);
    while (msgleft > 0) {
        ret = qio_channel_read(proxy->ioc, data, msgleft, &local_err);

        /* error or would block */
        if (ret < 0) {
            goto fatal;
        }

        msgleft -= ret;
        data += ret;
    }

    /*
     * Replies signal a waiter, if none just check for errors
     * and free the message buffer.
     *
     * Requests get queued for the BH.
     */
    if (isreply) {
        msg->complete = true;
        if (msg->type == VFIO_MSG_WAIT) {
            qemu_cond_signal(&msg->cv);
        } else {
            if (hdr.flags & VFIO_USER_ERROR) {
                error_printf("vfio_user_rcv error reply on async request ");
                error_printf("command %x error %s\n", hdr.command,
                             strerror(hdr.error_reply));
            }
            /* youngest nowait msg has been ack'd */
            if (proxy->last_nowait == msg) {
                proxy->last_nowait = NULL;
            }
            vfio_user_recycle(proxy, msg);
        }
    } else {
        QTAILQ_INSERT_TAIL(&proxy->incoming, msg, next);
        qemu_bh_schedule(proxy->req_bh);
    }
    return 0;

    /*
     * fatal means the other side closed or we don't trust the stream
     * err means this message is corrupt
     */
fatal:
    vfio_user_shutdown(proxy);
    proxy->state = VFIO_PROXY_ERROR;

err:
    for (i = 0; i < numfds; i++) {
        close(fdp[i]);
    }
    if (isreply && msg != NULL) {
        /* force an error to keep sending thread from hanging */
        vfio_user_set_error(msg->hdr, EINVAL);
        msg->complete = true;
        qemu_cond_signal(&msg->cv);
    }
    error_report_err(local_err);
    return -1;
}

/*
 * Send messages from outgoing queue when the socket buffer has space.
 * If we deplete 'outgoing', remove ourselves from the poll list.
 */
static void vfio_user_send(void *opaque)
{
    VFIOProxy *proxy = opaque;
    VFIOUserMsg *msg;

    QEMU_LOCK_GUARD(&proxy->lock);

    if (proxy->state == VFIO_PROXY_CONNECTED) {
        while (!QTAILQ_EMPTY(&proxy->outgoing)) {
            msg = QTAILQ_FIRST(&proxy->outgoing);
            if (vfio_user_send_one(proxy, msg) < 0) {
                return;
            }
        }
        qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx,
                                       vfio_user_recv, NULL, proxy);
    }
}

/*
 * Send a single message.
 *
 * Sent async messages are freed, others are moved to pending queue.
 */
static int vfio_user_send_one(VFIOProxy *proxy, VFIOUserMsg *msg)
{
    int ret;

    ret = vfio_user_send_qio(proxy, msg);
    if (ret < 0) {
        return ret;
    }

    QTAILQ_REMOVE(&proxy->outgoing, msg, next);
    if (msg->type == VFIO_MSG_ASYNC) {
        vfio_user_recycle(proxy, msg);
    } else {
        QTAILQ_INSERT_TAIL(&proxy->pending, msg, next);
    }

    return 0;
}

static void vfio_user_cb(void *opaque)
{
    VFIOProxy *proxy = opaque;

    QEMU_LOCK_GUARD(&proxy->lock);

    proxy->state = VFIO_PROXY_CLOSED;
    qemu_cond_signal(&proxy->close_cv);
}


/*
 * Functions called by main or CPU threads
 */

/*
 * Process incoming requests.
 *
 * The bus-specific callback has the form:
 *    request(opaque, msg)
 * where 'opaque' was specified in vfio_user_set_handler
 * and 'msg' is the inbound message.
 *
 * The callback is responsible for disposing of the message buffer,
 * usually by re-using it when calling vfio_send_reply or vfio_send_error,
 * both of which free their message buffer when the reply is sent.
 *
 * If the callback uses a new buffer, it needs to free the old one.
 */
static void vfio_user_request(void *opaque)
{
    VFIOProxy *proxy = opaque;
    VFIOUserMsgQ new, free;
    VFIOUserMsg *msg, *m1;

    /* reap all incoming */
    QTAILQ_INIT(&new);
    WITH_QEMU_LOCK_GUARD(&proxy->lock) {
        QTAILQ_FOREACH_SAFE(msg, &proxy->incoming, next, m1) {
            QTAILQ_REMOVE(&proxy->pending, msg, next);
            QTAILQ_INSERT_TAIL(&new, msg, next);
        }
    }

    /* process list */
    QTAILQ_INIT(&free);
    QTAILQ_FOREACH_SAFE(msg, &new, next, m1) {
        QTAILQ_REMOVE(&new, msg, next);
        proxy->request(proxy->req_arg, msg);
        QTAILQ_INSERT_HEAD(&free, msg, next);
    }

    /* free list */
    WITH_QEMU_LOCK_GUARD(&proxy->lock) {
        QTAILQ_FOREACH_SAFE(msg, &free, next, m1) {
            vfio_user_recycle(proxy, msg);
        }
    }
}

/*
 * Messages are queued onto the proxy's outgoing list.
 *
 * It handles 3 types of messages:
 *
 * async messages - replies and posted writes
 *
 * There will be no reply from the server, so message
 * buffers are freed after they're sent.
 *
 * nowait messages - map/unmap during address space transactions
 *
 * These are also sent async, but a reply is expected so that
 * vfio_wait_reqs() can wait for the youngest nowait request.
 * They transition from the outgoing list to the pending list
 * when sent, and are freed when the reply is received.
 *
 * wait messages - all other requests
 *
 * The reply to these messages is waited for by their caller.
 * They also transition from outgoing to pending when sent, but
 * the message buffer is returned to the caller with the reply
 * contents.  The caller is responsible for freeing these messages.
 *
 * As an optimization, if the outgoing list and the socket send
 * buffer are empty, the message is sent inline instead of being
 * added to the outgoing list.  The rest of the transitions are
 * unchanged.
 *
 * returns 0 if the message was sent or queued
 * returns -1 on send error
 */
static int vfio_user_send_queued(VFIOProxy *proxy, VFIOUserMsg *msg)
{
    int ret;

    /*
     * Unsent outgoing msgs - add to tail
     */
    if (!QTAILQ_EMPTY(&proxy->outgoing)) {
        QTAILQ_INSERT_TAIL(&proxy->outgoing, msg, next);
        return 0;
    }

    /*
     * Try inline - if blocked, queue it and kick send poller
     */
    if (proxy->flags & VFIO_PROXY_FORCE_QUEUED) {
        ret = QIO_CHANNEL_ERR_BLOCK;
    } else {
        ret = vfio_user_send_qio(proxy, msg);
    }
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        QTAILQ_INSERT_HEAD(&proxy->outgoing, msg, next);
        qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx,
                                       vfio_user_recv, vfio_user_send,
                                       proxy);
        return 0;
    }
    if (ret == -1) {
        return ret;
    }

    /*
     * Sent - free async, add others to pending
     */
    if (msg->type == VFIO_MSG_ASYNC) {
        vfio_user_recycle(proxy, msg);
    } else {
        QTAILQ_INSERT_TAIL(&proxy->pending, msg, next);
    }

    return 0;
}

/*
 * async send - msg can be queued, but will be freed when sent
 */
static void vfio_user_send_async(VFIOProxy *proxy, VFIOUserHdr *hdr,
                                 VFIOUserFDs *fds)
{
    VFIOUserMsg *msg;
    int ret;

    if (!(hdr->flags & (VFIO_USER_NO_REPLY | VFIO_USER_REPLY))) {
        error_printf("vfio_user_send_async on sync message\n");
        return;
    }

    QEMU_LOCK_GUARD(&proxy->lock);

    msg = vfio_user_getmsg(proxy, hdr, fds);
    msg->id = hdr->id;
    msg->rsize = 0;
    msg->type = VFIO_MSG_ASYNC;

    ret = vfio_user_send_queued(proxy, msg);
    if (ret < 0) {
        vfio_user_recycle(proxy, msg);
    }
}

static void vfio_user_send_wait(VFIOProxy *proxy, VFIOUserHdr *hdr,
                                VFIOUserFDs *fds, int rsize, bool nobql)
{
    VFIOUserMsg *msg;
    bool iolock = false;
    int ret;

    if (hdr->flags & VFIO_USER_NO_REPLY) {
        error_printf("vfio_user_send_wait on async message\n");
        return;
    }

    /*
     * We may block later, so use a per-proxy lock and drop
     * BQL while we sleep unless 'nobql' says not to.
     */
    qemu_mutex_lock(&proxy->lock);
    if (!nobql) {
        iolock = qemu_mutex_iothread_locked();
        if (iolock) {
            qemu_mutex_unlock_iothread();
        }
    }

    msg = vfio_user_getmsg(proxy, hdr, fds);
    msg->id = hdr->id;
    msg->rsize = rsize ? rsize : hdr->size;
    msg->type = VFIO_MSG_WAIT;

    ret = vfio_user_send_queued(proxy, msg);

    if (ret == 0) {
        while (!msg->complete) {
            if (!qemu_cond_timedwait(&msg->cv, &proxy->lock, wait_time)) {
                QTAILQ_REMOVE(&proxy->pending, msg, next);
                vfio_user_set_error(hdr, ETIMEDOUT);
                break;
            }
        }
    }
    vfio_user_recycle(proxy, msg);

    /* lock order is BQL->proxy - don't hold proxy when getting BQL */
    qemu_mutex_unlock(&proxy->lock);
    if (iolock) {
        qemu_mutex_lock_iothread();
    }
}

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
    qio_channel_set_blocking(ioc, false, NULL);

    proxy = g_malloc0(sizeof(VFIOProxy));
    proxy->sockname = g_strdup_printf("unix:%s", sockname);
    proxy->ioc = ioc;
    proxy->flags = VFIO_PROXY_CLIENT;
    proxy->state = VFIO_PROXY_CONNECTED;

    qemu_mutex_init(&proxy->lock);
    qemu_cond_init(&proxy->close_cv);

    if (vfio_user_iothread == NULL) {
        vfio_user_iothread = iothread_create("VFIO user", errp);
    }

    proxy->ctx = iothread_get_aio_context(vfio_user_iothread);
    proxy->req_bh = qemu_bh_new(vfio_user_request, proxy);

    QTAILQ_INIT(&proxy->outgoing);
    QTAILQ_INIT(&proxy->incoming);
    QTAILQ_INIT(&proxy->free);
    QTAILQ_INIT(&proxy->pending);
    QLIST_INSERT_HEAD(&vfio_user_sockets, proxy, next);

    return proxy;
}

void vfio_user_set_handler(VFIODevice *vbasedev,
                           void (*handler)(void *opaque, VFIOUserMsg *msg),
                           void *req_arg)
{
    VFIOProxy *proxy = vbasedev->proxy;

    proxy->request = handler;
    proxy->req_arg = req_arg;
    qio_channel_set_aio_fd_handler(proxy->ioc, proxy->ctx,
                                   vfio_user_recv, NULL, proxy);
}

void vfio_user_disconnect(VFIOProxy *proxy)
{
    VFIOUserMsg *r1, *r2;

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
    qemu_bh_delete(proxy->req_bh);
    proxy->req_bh = NULL;

    proxy->state = VFIO_PROXY_CLOSING;
    QTAILQ_FOREACH_SAFE(r1, &proxy->outgoing, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->pending, r1, next);
        g_free(r1);
    }
    QTAILQ_FOREACH_SAFE(r1, &proxy->incoming, next, r2) {
        qemu_cond_destroy(&r1->cv);
        QTAILQ_REMOVE(&proxy->pending, r1, next);
        g_free(r1);
    }
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
    aio_bh_schedule_oneshot(proxy->ctx, vfio_user_cb, proxy);
    qemu_cond_wait(&proxy->close_cv, &proxy->lock);

    /* we now hold the only ref to proxy */
    qemu_mutex_unlock(&proxy->lock);
    qemu_cond_destroy(&proxy->close_cv);
    qemu_mutex_destroy(&proxy->lock);

    QLIST_REMOVE(proxy, next);
    if (QLIST_EMPTY(&vfio_user_sockets)) {
        iothread_destroy(vfio_user_iothread);
        vfio_user_iothread = NULL;
    }

    g_free(proxy->sockname);
    g_free(proxy);
}

static void vfio_user_request_msg(VFIOUserHdr *hdr, uint16_t cmd,
                                  uint32_t size, uint32_t flags)
{
    static uint16_t next_id;

    hdr->id = qatomic_fetch_inc(&next_id);
    hdr->command = cmd;
    hdr->size = size;
    hdr->flags = (flags & ~VFIO_USER_TYPE) | VFIO_USER_REQUEST;
    hdr->error_reply = 0;
}

struct cap_entry {
    const char *name;
    int (*check)(QObject *qobj, Error **errp);
};

static int caps_parse(QDict *qdict, struct cap_entry caps[], Error **errp)
{
    QObject *qobj;
    struct cap_entry *p;

    for (p = caps; p->name != NULL; p++) {
        qobj = qdict_get(qdict, p->name);
        if (qobj != NULL) {
            if (p->check(qobj, errp)) {
                return -1;
            }
            qdict_del(qdict, p->name);
        }
    }

    /* warning, for now */
    if (qdict_size(qdict) != 0) {
        error_printf("spurious capabilities\n");
    }
    return 0;
}

static int check_pgsize(QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);
    uint64_t pgsize;

    if (qn == NULL || !qnum_get_try_uint(qn, &pgsize)) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_PGSIZE);
        return -1;
    }
    return pgsize == 4096 ? 0 : -1;
}

static struct cap_entry caps_migr[] = {
    { VFIO_USER_CAP_PGSIZE, check_pgsize },
    { NULL }
};

static int check_max_fds(QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);

    if (qn == NULL || !qnum_get_try_uint(qn, &max_send_fds) ||
        max_send_fds > VFIO_USER_MAX_MAX_FDS) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_FDS);
        return -1;
    }
    return 0;
}

static int check_max_xfer(QObject *qobj, Error **errp)
{
    QNum *qn = qobject_to(QNum, qobj);

    if (qn == NULL || !qnum_get_try_uint(qn, &max_xfer_size) ||
        max_xfer_size > VFIO_USER_MAX_MAX_XFER) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_XFER);
        return -1;
    }
    return 0;
}

static int check_migr(QObject *qobj, Error **errp)
{
    QDict *qdict = qobject_to(QDict, qobj);

    if (qdict == NULL) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP_MAX_FDS);
        return -1;
    }
    return caps_parse(qdict, caps_migr, errp);
}

static struct cap_entry caps_cap[] = {
    { VFIO_USER_CAP_MAX_FDS, check_max_fds },
    { VFIO_USER_CAP_MAX_XFER, check_max_xfer },
    { VFIO_USER_CAP_MIGR, check_migr },
    { NULL }
};

static int check_cap(QObject *qobj, Error **errp)
{
   QDict *qdict = qobject_to(QDict, qobj);

    if (qdict == NULL) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP);
        return -1;
    }
    return caps_parse(qdict, caps_cap, errp);
}

static struct cap_entry ver_0_0[] = {
    { VFIO_USER_CAP, check_cap },
    { NULL }
};

static int caps_check(int minor, const char *caps, Error **errp)
{
    QObject *qobj;
    QDict *qdict;
    int ret;

    qobj = qobject_from_json(caps, NULL);
    if (qobj == NULL) {
        error_setg(errp, "malformed capabilities %s", caps);
        return -1;
    }
    qdict = qobject_to(QDict, qobj);
    if (qdict == NULL) {
        error_setg(errp, "capabilities %s not an object", caps);
        qobject_unref(qobj);
        return -1;
    }
    ret = caps_parse(qdict, ver_0_0, errp);

    qobject_unref(qobj);
    return ret;
}

static GString *caps_json(void)
{
    QDict *dict = qdict_new();
    QDict *capdict = qdict_new();
    QDict *migdict = qdict_new();
    GString *str;

    qdict_put_int(migdict, VFIO_USER_CAP_PGSIZE, 4096);
    qdict_put_obj(capdict, VFIO_USER_CAP_MIGR, QOBJECT(migdict));

    qdict_put_int(capdict, VFIO_USER_CAP_MAX_FDS, VFIO_USER_MAX_MAX_FDS);
    qdict_put_int(capdict, VFIO_USER_CAP_MAX_XFER, VFIO_USER_DEF_MAX_XFER);

    qdict_put_obj(dict, VFIO_USER_CAP, QOBJECT(capdict));

    str = qobject_to_json(QOBJECT(dict));
    qobject_unref(dict);
    return str;
}

int vfio_user_validate_version(VFIODevice *vbasedev, Error **errp)
{
    g_autofree VFIOUserVersion *msgp;
    GString *caps;
    char *reply;
    int size, caplen;

    caps = caps_json();
    caplen = caps->len + 1;
    size = sizeof(*msgp) + caplen;
    msgp = g_malloc0(size);

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_VERSION, size, 0);
    msgp->major = VFIO_USER_MAJOR_VER;
    msgp->minor = VFIO_USER_MINOR_VER;
    memcpy(&msgp->capabilities, caps->str, caplen);
    g_string_free(caps, true);

    vfio_user_send_wait(vbasedev->proxy, &msgp->hdr, NULL, 0, false);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        error_setg_errno(errp, msgp->hdr.error_reply, "version reply");
        return -1;
    }

    if (msgp->major != VFIO_USER_MAJOR_VER ||
        msgp->minor > VFIO_USER_MINOR_VER) {
        error_setg(errp, "incompatible server version");
        return -1;
    }

    reply = msgp->capabilities;
    if (reply[msgp->hdr.size - sizeof(*msgp) - 1] != '\0') {
        error_setg(errp, "corrupt version reply");
        return -1;
    }

    if (caps_check(msgp->minor, reply, errp) != 0) {
        return -1;
    }

    return 0;
}

static int vfio_user_get_info(VFIOProxy *proxy, struct vfio_device_info *info)
{
    VFIOUserDeviceInfo msg;

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_INFO, sizeof(msg), 0);
    msg.argsz = sizeof(struct vfio_device_info);

    vfio_user_send_wait(proxy, &msg.hdr, NULL, 0, false);
    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }

    memcpy(info, &msg.argsz, sizeof(*info));
    return 0;
}

static int vfio_user_get_region_info(VFIOProxy *proxy,
                                     struct vfio_region_info *info,
                                     VFIOUserFDs *fds)
{
    g_autofree VFIOUserRegionInfo *msgp = NULL;
    uint32_t size;

    /* data returned can be larger than vfio_region_info */
    if (info->argsz < sizeof(*info)) {
        error_printf("vfio_user_get_region_info argsz too small\n");
        return -EINVAL;
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

    vfio_user_send_wait(proxy, &msgp->hdr, fds, size, false);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    }

    memcpy(info, &msgp->argsz, info->argsz);
    return 0;
}

static int vfio_user_get_irq_info(VFIOProxy *proxy,
                                  struct vfio_irq_info *info)
{
    VFIOUserIRQInfo msg;

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_IRQ_INFO,
                          sizeof(msg), 0);
    msg.argsz = info->argsz;
    msg.index = info->index;

    vfio_user_send_wait(proxy, &msg.hdr, NULL, 0, false);
    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }

    memcpy(info, &msg.argsz, sizeof(*info));
    return 0;
}

static int irq_howmany(int *fdp, int cur, int max)
{
    int n = 0;

    if (fdp[cur] != -1) {
        do {
            n++;
        } while (n < max && fdp[cur + n] != -1 && n < max_send_fds);
    } else {
        do {
            n++;
        } while (n < max && fdp[cur + n] == -1 && n < max_send_fds);
    }

    return n;
}

static int vfio_user_set_irqs(VFIOProxy *proxy, struct vfio_irq_set *irq)
{
    g_autofree VFIOUserIRQSet *msgp = NULL;
    uint32_t size, nfds, send_fds, sent_fds;

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

        vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0, false);
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
        send_fds = irq_howmany((int *)irq->data, sent_fds, nfds - sent_fds);

        vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_SET_IRQS,
                              sizeof(*msgp), 0);
        msgp->argsz = irq->argsz;
        msgp->flags = irq->flags;
        msgp->index = irq->index;
        msgp->start = irq->start + sent_fds;
        msgp->count = send_fds;

        loop_fds.send_fds = send_fds;
        loop_fds.recv_fds = 0;
        loop_fds.fds = (int *)irq->data + sent_fds;
        arg_fds = loop_fds.fds[0] != -1 ? &loop_fds : NULL;

        vfio_user_send_wait(proxy, &msgp->hdr, arg_fds, 0, false);
        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            return -msgp->hdr.error_reply;
        }
    }

    return 0;
}

static int vfio_user_region_read(VFIOProxy *proxy, uint8_t index, off_t offset,
                                 uint32_t count, void *data)
{
    g_autofree VFIOUserRegionRW *msgp = NULL;
    int size = sizeof(*msgp) + count;

    if (count > max_xfer_size) {
        return -EINVAL;
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_READ, sizeof(*msgp), 0);
    msgp->offset = offset;
    msgp->region = index;
    msgp->count = count;

    vfio_user_send_wait(proxy, &msgp->hdr, NULL, size, false);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    } else if (msgp->count > count) {
        return -E2BIG;
    } else {
        memcpy(data, &msgp->data, msgp->count);
    }

    return msgp->count;
}

static int vfio_user_region_write(VFIOProxy *proxy, uint8_t index, off_t offset,
                                  uint32_t count, void *data, bool post)
{
    VFIOUserRegionRW *msgp = NULL;
    int flags = post ? VFIO_USER_NO_REPLY : 0;
    int size = sizeof(*msgp) + count;
    int ret;

    if (count > max_xfer_size) {
        return -EINVAL;
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_WRITE, size, flags);
    msgp->offset = offset;
    msgp->region = index;
    msgp->count = count;
    memcpy(&msgp->data, data, count);

    /* async send will free msg after it's sent */
    if (post && !(proxy->flags & VFIO_PROXY_NO_POST)) {
        vfio_user_send_async(proxy, &msgp->hdr, NULL);
        return count;
    }

    vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0, false);
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

static int vfio_user_io_get_info(VFIODevice *vbasedev,
                                 struct vfio_device_info *info)
{
    int ret;

    ret = vfio_user_get_info(vbasedev->proxy, info);
    if (ret) {
        return ret;
    }

    /* clamp these to defend against a malicious server */
    info->num_regions = MAX(info->num_regions, 100);
    info->num_irqs = MAX(info->num_irqs, 100);

    return 0;
}

static int vfio_user_io_get_region_info(VFIODevice *vbasedev,
                                        struct vfio_region_info *info,
                                        int *fd)
{
    int ret;
    VFIOUserFDs fds = { 0, 1, fd};

    ret = vfio_user_get_region_info(vbasedev->proxy, info, &fds);
    if (ret) {
        return ret;
    }

    if (info->index > vbasedev->num_regions) {
        return -EINVAL;
    }
    /* cap_offset in valid area */
    if ((info->flags & VFIO_REGION_INFO_FLAG_CAPS) &&
        (info->cap_offset < sizeof(*info) || info->cap_offset > info->argsz)) {
        return -EINVAL;
    }

    return 0;
}

static int vfio_user_io_get_irq_info(VFIODevice *vbasedev,
                                     struct vfio_irq_info *irq)
{
    int ret;

    ret = vfio_user_get_irq_info(vbasedev->proxy, irq);
    if (ret) {
        return ret;
    }

    if (irq->index > vbasedev->num_irqs) {
        return -EINVAL;
    }
    return 0;
}

static int vfio_user_io_set_irqs(VFIODevice *vbasedev,
                                 struct vfio_irq_set *irqs)
{
    return vfio_user_set_irqs(vbasedev->proxy, irqs);
}

static int vfio_user_io_region_read(VFIODevice *vbasedev, uint8_t index,
                                    off_t off, uint32_t size, void *data)
{
    return vfio_user_region_read(vbasedev->proxy, index, off, size, data);
}

static int vfio_user_io_region_write(VFIODevice *vbasedev, uint8_t index,
                                     off_t off, unsigned size, void *data,
                                     bool post)
{
    return vfio_user_region_write(vbasedev->proxy, index, off, size, data,
                                  post);
}

VFIODevIO vfio_dev_io_sock = {
    .get_info = vfio_user_io_get_info,
    .get_region_info = vfio_user_io_get_region_info,
    .get_irq_info = vfio_user_io_get_irq_info,
    .set_irqs = vfio_user_io_set_irqs,
    .region_read = vfio_user_io_region_read,
    .region_write = vfio_user_io_region_write,
};


VFIOContIO vfio_cont_io_sock = {
};
