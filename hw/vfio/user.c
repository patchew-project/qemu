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

uint64_t vfio_user_max_xfer(void)
{
    return max_xfer_size;
}

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
        if (msg.size > max_xfer_size + sizeof(struct vfio_user_dma_rw)) {
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

int vfio_user_get_info(VFIODevice *vbasedev)
{
    struct vfio_user_device_info msg;

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_INFO, sizeof(msg), 0);
    msg.argsz = sizeof(struct vfio_device_info);

    vfio_user_send_recv(vbasedev->proxy, &msg.hdr, NULL, 0);
    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }

    vbasedev->num_irqs = msg.num_irqs;
    vbasedev->num_regions = msg.num_regions;
    vbasedev->flags = msg.flags;
    vbasedev->reset_works = !!(msg.flags & VFIO_DEVICE_FLAGS_RESET);
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

static void vfio_user_cb(void *opaque)
{
    VFIOProxy *proxy = opaque;

    qemu_mutex_lock(&proxy->lock);
    proxy->state = CLOSED;
    qemu_mutex_unlock(&proxy->lock);
    qemu_cond_signal(&proxy->close_cv);
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

int vfio_user_get_irq_info(VFIODevice *vbasedev, struct vfio_irq_info *info)
{
    struct vfio_user_irq_info msg;

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_IRQ_INFO,
                          sizeof(msg), 0);
    msg.argsz = info->argsz;
    msg.index = info->index;

    vfio_user_send_recv(vbasedev->proxy, &msg.hdr, NULL, 0);
    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }

    memcpy(info, &msg.argsz, sizeof(*info));
    return 0;
}

int vfio_user_region_read(VFIODevice *vbasedev, uint32_t index, uint64_t offset,
                                 uint32_t count, void *data)
{
    g_autofree struct vfio_user_region_rw *msgp = NULL;
    int size = sizeof(*msgp) + count;

    /* most reads are just registers, only allocate for larger ones */
    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_READ, sizeof(*msgp), 0);
    msgp->offset = offset;
    msgp->region = index;
    msgp->count = count;

    vfio_user_send_recv(vbasedev->proxy, &msgp->hdr, NULL, size);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    } else if (msgp->count > count) {
        return -E2BIG;
    } else {
        memcpy(data, &msgp->data, msgp->count);
    }

    return msgp->count;
}

int vfio_user_region_write(VFIODevice *vbasedev, uint32_t index,
                           uint64_t offset, uint32_t count, void *data)
{
    g_autofree struct vfio_user_region_rw *msgp = NULL;
    int size = sizeof(*msgp) + count;

    /* most writes are just registers, only allocate for larger ones */
    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_WRITE, size,
                          VFIO_USER_NO_REPLY);
    msgp->offset = offset;
    msgp->region = index;
    msgp->count = count;
    memcpy(&msgp->data, data, count);

    vfio_user_send(vbasedev->proxy, &msgp->hdr, NULL);

    return count;
}

int vfio_user_dma_map(VFIOProxy *proxy, struct vfio_iommu_type1_dma_map *map,
                      VFIOUserFDs *fds)
{
    struct vfio_user_dma_map msg;
    int ret;

    vfio_user_request_msg(&msg.hdr, VFIO_USER_DMA_MAP, sizeof(msg), 0);
    msg.argsz = map->argsz;
    msg.flags = map->flags;
    msg.offset = map->vaddr;
    msg.iova = map->iova;
    msg.size = map->size;

    vfio_user_send_recv(proxy, &msg.hdr, fds, 0);
    ret = (msg.hdr.flags & VFIO_USER_ERROR) ? -msg.hdr.error_reply : 0;
    return ret;
}

int vfio_user_dma_unmap(VFIOProxy *proxy,
                        struct vfio_iommu_type1_dma_unmap *unmap,
                        struct vfio_bitmap *bitmap)
{
    g_autofree struct {
        struct vfio_user_dma_unmap msg;
        struct vfio_user_bitmap bitmap;
    } *msgp = NULL;
    int msize, rsize;

    if (bitmap == NULL && (unmap->flags &
                           VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP)) {
        error_printf("vfio_user_dma_unmap mismatched flags and bitmap\n");
        return -EINVAL;
    }

    /*
     * If a dirty bitmap is returned, allocate extra space for it
     * otherwise, just send the unmap request
     */
    if (bitmap != NULL) {
        msize = sizeof(*msgp);
        rsize = msize + bitmap->size;
        msgp = g_malloc0(rsize);
        msgp->bitmap.pgsize = bitmap->pgsize;
        msgp->bitmap.size = bitmap->size;
    } else {
        msize = rsize = sizeof(struct vfio_user_dma_unmap);
        msgp = g_malloc0(rsize);
    }

    vfio_user_request_msg(&msgp->msg.hdr, VFIO_USER_DMA_UNMAP, msize, 0);
    msgp->msg.argsz = unmap->argsz;
    msgp->msg.flags = unmap->flags;
    msgp->msg.iova = unmap->iova;
    msgp->msg.size = unmap->size;

    vfio_user_send_recv(proxy, &msgp->msg.hdr, NULL, rsize);
    if (msgp->msg.hdr.flags & VFIO_USER_ERROR) {
        return -msgp->msg.hdr.error_reply;
    }

    if (bitmap != NULL) {
        memcpy(bitmap->data, &msgp->bitmap.data, bitmap->size);
    }

    return 0;
}

int vfio_user_get_region_info(VFIODevice *vbasedev, int index,
                              struct vfio_region_info *info, VFIOUserFDs *fds)
{
    g_autofree struct vfio_user_region_info *msgp = NULL;
    int size;

    /* data returned can be larger than vfio_region_info */
    if (info->argsz < sizeof(*info)) {
        error_printf("vfio_user_get_region_info argsz too small\n");
        return -EINVAL;
    }
    if (fds != NULL && fds->send_fds != 0) {
        error_printf("vfio_user_get_region_info can't send FDs\n");
        return -EINVAL;
    }

    size = info->argsz + sizeof(vfio_user_hdr_t);
    msgp = g_malloc0(size);

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_GET_REGION_INFO,
                          sizeof(*msgp), 0);
    msgp->argsz = info->argsz;
    msgp->index = info->index;

    vfio_user_send_recv(vbasedev->proxy, &msgp->hdr, fds, size);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    }

    memcpy(info, &msgp->argsz, info->argsz);
    return 0;
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

int vfio_user_set_irqs(VFIODevice *vbasedev, struct vfio_irq_set *irq)
{
    g_autofree struct vfio_user_irq_set *msgp = NULL;
    uint32_t size, nfds, send_fds, sent_fds;

    if (irq->argsz < sizeof(*irq)) {
        error_printf("vfio_user_set_irqs argsz too small\n");
        return -EINVAL;
    }

    /*
     * Handle simple case
     */
    if ((irq->flags & VFIO_IRQ_SET_DATA_EVENTFD) == 0) {
        size = sizeof(vfio_user_hdr_t) + irq->argsz;
        msgp = g_malloc0(size);

        vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_SET_IRQS, size, 0);
        msgp->argsz = irq->argsz;
        msgp->flags = irq->flags;
        msgp->index = irq->index;
        msgp->start = irq->start;
        msgp->count = irq->count;

        vfio_user_send_recv(vbasedev->proxy, &msgp->hdr, NULL, 0);
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

        vfio_user_send_recv(vbasedev->proxy, &msgp->hdr, arg_fds, 0);
        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            return -msgp->hdr.error_reply;
        }
    }

    return 0;
}
