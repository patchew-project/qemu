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
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qnum.h"
#include "user.h"

static uint64_t max_xfer_size = VFIO_USER_DEF_MAX_XFER;
static uint64_t max_send_fds = VFIO_USER_DEF_MAX_FDS;
static IOThread *vfio_user_iothread;
static void vfio_user_send_locked(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                                  VFIOUserFDs *fds);
static void vfio_user_send(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                           VFIOUserFDs *fds);
static void vfio_user_shutdown(VFIOProxy *proxy);
static void vfio_user_request_msg(vfio_user_hdr_t *hdr, uint16_t cmd,
                                  uint32_t size, uint32_t flags);
static void vfio_user_send_recv(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                                VFIOUserFDs *fds, int rsize);

static void vfio_user_shutdown(VFIOProxy *proxy)
{
    qio_channel_shutdown(proxy->ioc, QIO_CHANNEL_SHUTDOWN_READ, NULL);
    qio_channel_set_aio_fd_handler(proxy->ioc,
                                   iothread_get_aio_context(vfio_user_iothread),
                                   NULL, NULL, NULL);
}

static void vfio_user_request_msg(vfio_user_hdr_t *hdr, uint16_t cmd,
                                  uint32_t size, uint32_t flags)
{
    static uint16_t next_id;

    hdr->id = qatomic_fetch_inc(&next_id);
    hdr->command = cmd;
    hdr->size = size;
    hdr->flags = (flags & ~VFIO_USER_TYPE) | VFIO_USER_REQUEST;
    hdr->error_reply = 0;
}

static int wait_time = 1000;   /* wait 1 sec for replies */

static void vfio_user_send_recv(VFIOProxy *proxy, vfio_user_hdr_t *msg,
                                VFIOUserFDs *fds, int rsize)
{
    VFIOUserReply *reply;
    bool iolock = qemu_mutex_iothread_locked();

    if (msg->flags & VFIO_USER_NO_REPLY) {
        error_printf("vfio_user_send_recv on async message\n");
        return;
    }

    /*
     * We will block later, so use a per-proxy lock and let
     * the iothreads run while we sleep.
     */
    if (iolock) {
        qemu_mutex_unlock_iothread();
    }
    qemu_mutex_lock(&proxy->lock);

    reply = QTAILQ_FIRST(&proxy->free);
    if (reply != NULL) {
        QTAILQ_REMOVE(&proxy->free, reply, next);
        reply->complete = 0;
    } else {
        reply = g_malloc0(sizeof(*reply));
        qemu_cond_init(&reply->cv);
    }
    reply->msg = msg;
    reply->fds = fds;
    reply->id = msg->id;
    reply->rsize = rsize ? rsize : msg->size;
    QTAILQ_INSERT_TAIL(&proxy->pending, reply, next);

    vfio_user_send_locked(proxy, msg, fds);
    if ((msg->flags & VFIO_USER_ERROR) == 0) {
        while (reply->complete == 0) {
            if (!qemu_cond_timedwait(&reply->cv, &proxy->lock, wait_time)) {
                msg->flags |= VFIO_USER_ERROR;
                msg->error_reply = ETIMEDOUT;
                break;
            }
        }
    }

    QTAILQ_INSERT_HEAD(&proxy->free, reply, next);
    qemu_mutex_unlock(&proxy->lock);
    if (iolock) {
        qemu_mutex_lock_iothread();
    }
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

static struct cap_entry caps_cap[] = {
    { VFIO_USER_CAP_MAX_FDS, check_max_fds },
    { VFIO_USER_CAP_MAX_XFER, check_max_xfer },
    { NULL }
};

static int check_cap(QObject *qobj, Error **errp)
{
   QDict *qdict = qobject_to(QDict, qobj);

    if (qdict == NULL || caps_parse(qdict, caps_cap, errp)) {
        error_setg(errp, "malformed %s", VFIO_USER_CAP);
        return -1;
    }
    return 0;
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
    GString *str;

    qdict_put_int(capdict, VFIO_USER_CAP_MAX_FDS, VFIO_USER_MAX_MAX_FDS);
    qdict_put_int(capdict, VFIO_USER_CAP_MAX_XFER, VFIO_USER_DEF_MAX_XFER);

    qdict_put_obj(dict, VFIO_USER_CAP, QOBJECT(capdict));

    str = qobject_to_json(QOBJECT(dict));
    qobject_unref(dict);
    return str;
}

int vfio_user_validate_version(VFIODevice *vbasedev, Error **errp)
{
    g_autofree struct vfio_user_version *msgp;
    GString *caps;
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

    vfio_user_send_recv(vbasedev->proxy, &msgp->hdr, NULL, 0);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        error_setg_errno(errp, msgp->hdr.error_reply, "version reply");
        return -1;
    }

    if (msgp->major != VFIO_USER_MAJOR_VER ||
        msgp->minor > VFIO_USER_MINOR_VER) {
        error_setg(errp, "incompatible server version");
        return -1;
    }
    if (caps_check(msgp->minor, (char *)msgp + sizeof(*msgp), errp) != 0) {
        return -1;
    }

    return 0;
}

static QLIST_HEAD(, VFIOProxy) vfio_user_sockets =
    QLIST_HEAD_INITIALIZER(vfio_user_sockets);

VFIOProxy *vfio_user_connect_dev(char *sockname, Error **errp)
{
    VFIOProxy *proxy;
    struct QIOChannel *ioc;
    int sockfd;

    sockfd = unix_connect(sockname, errp);
    if (sockfd == -1) {
        return NULL;
    }

    ioc = qio_channel_new_fd(sockfd, errp);
    if (ioc == NULL) {
        close(sockfd);
        return NULL;
    }
    qio_channel_set_blocking(ioc, true, NULL);

    proxy = g_malloc0(sizeof(VFIOProxy));
    proxy->sockname = sockname;
    proxy->ioc = ioc;
    proxy->flags = VFIO_PROXY_CLIENT;
    proxy->state = CONNECTED;
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

void vfio_user_disconnect(VFIOProxy *proxy)
{
    VFIOUserReply *r1, *r2;

    qemu_mutex_lock(&proxy->lock);

    /* our side is quitting */
    if (proxy->state == CONNECTED) {
        vfio_user_shutdown(proxy);
        if (!QTAILQ_EMPTY(&proxy->pending)) {
            error_printf("vfio_user_disconnect: outstanding requests\n");
        }
    }
    qio_channel_close(proxy->ioc, NULL);
    proxy->state = CLOSING;

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
