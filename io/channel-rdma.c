/*
 * QEMU I/O channels RDMA driver
 *
 * Copyright (c) 2024 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *  Jialin Wang <wangjialin23@huawei.com>
 *  Gonglei <arei.gonglei@huawei.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "io/channel-rdma.h"
#include "io/channel.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "trace.h"
#include <errno.h>
#include <netdb.h>
#include <rdma/rsocket.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unistd.h>

QIOChannelRDMA *qio_channel_rdma_new(void)
{
    QIOChannelRDMA *rioc;
    QIOChannel *ioc;

    rioc = QIO_CHANNEL_RDMA(object_new(TYPE_QIO_CHANNEL_RDMA));
    ioc = QIO_CHANNEL(rioc);
    qio_channel_set_feature(ioc, QIO_CHANNEL_FEATURE_SHUTDOWN);

    trace_qio_channel_rdma_new(ioc);

    return rioc;
}

static int qio_channel_rdma_set_fd(QIOChannelRDMA *rioc, int fd, Error **errp)
{
    if (rioc->fd != -1) {
        error_setg(errp, "Socket is already open");
        return -1;
    }

    rioc->fd = fd;
    rioc->remoteAddrLen = sizeof(rioc->remoteAddr);
    rioc->localAddrLen = sizeof(rioc->localAddr);

    if (rgetpeername(fd, (struct sockaddr *)&rioc->remoteAddr,
                     &rioc->remoteAddrLen) < 0) {
        if (errno == ENOTCONN) {
            memset(&rioc->remoteAddr, 0, sizeof(rioc->remoteAddr));
            rioc->remoteAddrLen = sizeof(rioc->remoteAddr);
        } else {
            error_setg_errno(errp, errno,
                             "Unable to query remote rsocket address");
            goto error;
        }
    }

    if (rgetsockname(fd, (struct sockaddr *)&rioc->localAddr,
                     &rioc->localAddrLen) < 0) {
        error_setg_errno(errp, errno, "Unable to query local rsocket address");
        goto error;
    }

    return 0;

error:
    rioc->fd = -1; /* Let the caller close FD on failure */
    return -1;
}

int qio_channel_rdma_connect_sync(QIOChannelRDMA *rioc, InetSocketAddress *addr,
                                  Error **errp)
{
    int ret, fd = -1;
    struct rdma_addrinfo *ai;

    trace_qio_channel_rdma_connect_sync(rioc, addr);
    ret = rdma_getaddrinfo(addr->host, addr->port, NULL, &ai);
    if (ret) {
        error_setg(errp, "Failed to rdma_getaddrinfo: %s", gai_strerror(ret));
        goto out;
    }

    fd = rsocket(ai->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to create rsocket");
        goto out;
    }
    qemu_set_cloexec(fd);

retry:
    ret = rconnect(fd, ai->ai_dst_addr, ai->ai_dst_len);
    if (ret) {
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "Failed to rconnect");
        goto out;
    }

    trace_qio_channel_rdma_connect_complete(rioc, fd);
    ret = qio_channel_rdma_set_fd(rioc, fd, errp);
    if (ret) {
        goto out;
    }

out:
    if (ret) {
        trace_qio_channel_rdma_connect_fail(rioc);
        if (fd >= 0) {
            rclose(fd);
        }
    }
    if (ai) {
        rdma_freeaddrinfo(ai);
    }

    return ret;
}

static void qio_channel_rdma_connect_worker(QIOTask *task, gpointer opaque)
{
    QIOChannelRDMA *ioc = QIO_CHANNEL_RDMA(qio_task_get_source(task));
    InetSocketAddress *addr = opaque;
    Error *err = NULL;

    qio_channel_rdma_connect_sync(ioc, addr, &err);

    qio_task_set_error(task, err);
}

void qio_channel_rdma_connect_async(QIOChannelRDMA *ioc,
                                    InetSocketAddress *addr,
                                    QIOTaskFunc callback, gpointer opaque,
                                    GDestroyNotify destroy,
                                    GMainContext *context)
{
    QIOTask *task = qio_task_new(OBJECT(ioc), callback, opaque, destroy);
    InetSocketAddress *addrCopy;

    addrCopy = QAPI_CLONE(InetSocketAddress, addr);

    /* rdma_getaddrinfo() blocks in DNS lookups, so we must use a thread */
    trace_qio_channel_rdma_connect_async(ioc, addr);
    qio_task_run_in_thread(task, qio_channel_rdma_connect_worker, addrCopy,
                           (GDestroyNotify)qapi_free_InetSocketAddress,
                           context);
}

int qio_channel_rdma_listen_sync(QIOChannelRDMA *rioc, InetSocketAddress *addr,
                                 int num, Error **errp)
{
    int ret, fd = -1;
    struct rdma_addrinfo *ai;
    struct rdma_addrinfo ai_hints = { 0 };

    trace_qio_channel_rdma_listen_sync(rioc, addr, num);
    ai_hints.ai_port_space = RDMA_PS_TCP;
    ai_hints.ai_flags |= RAI_PASSIVE;
    ret = rdma_getaddrinfo(addr->host, addr->port, &ai_hints, &ai);
    if (ret) {
        error_setg(errp, "Failed to rdma_getaddrinfo: %s", gai_strerror(ret));
        goto out;
    }

    fd = rsocket(ai->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to create rsocket");
        goto out;
    }
    qemu_set_cloexec(fd);

    ret = rbind(fd, ai->ai_src_addr, ai->ai_src_len);
    if (ret) {
        error_setg_errno(errp, errno, "Failed to rbind");
        goto out;
    }

    ret = rlisten(fd, num);
    if (ret) {
        error_setg_errno(errp, errno, "Failed to rlisten");
        goto out;
    }

    ret = qio_channel_rdma_set_fd(rioc, fd, errp);
    if (ret) {
        goto out;
    }

    qio_channel_set_feature(QIO_CHANNEL(rioc), QIO_CHANNEL_FEATURE_LISTEN);
    trace_qio_channel_rdma_listen_complete(rioc, fd);

out:
    if (ret) {
        trace_qio_channel_rdma_listen_fail(rioc);
        if (fd >= 0) {
            rclose(fd);
        }
    }
    if (ai) {
        rdma_freeaddrinfo(ai);
    }

    return ret;
}

struct QIOChannelListenWorkerData {
    InetSocketAddress *addr;
    int num; /* amount of expected connections */
};

static void qio_channel_listen_worker_free(gpointer opaque)
{
    struct QIOChannelListenWorkerData *data = opaque;

    qapi_free_InetSocketAddress(data->addr);
    g_free(data);
}

static void qio_channel_rdma_listen_worker(QIOTask *task, gpointer opaque)
{
    QIOChannelRDMA *ioc = QIO_CHANNEL_RDMA(qio_task_get_source(task));
    struct QIOChannelListenWorkerData *data = opaque;
    Error *err = NULL;

    qio_channel_rdma_listen_sync(ioc, data->addr, data->num, &err);

    qio_task_set_error(task, err);
}

void qio_channel_rdma_listen_async(QIOChannelRDMA *ioc, InetSocketAddress *addr,
                                   int num, QIOTaskFunc callback,
                                   gpointer opaque, GDestroyNotify destroy,
                                   GMainContext *context)
{
    QIOTask *task = qio_task_new(OBJECT(ioc), callback, opaque, destroy);
    struct QIOChannelListenWorkerData *data;

    data = g_new0(struct QIOChannelListenWorkerData, 1);
    data->addr = QAPI_CLONE(InetSocketAddress, addr);
    data->num = num;

    /* rdma_getaddrinfo() blocks in DNS lookups, so we must use a thread */
    trace_qio_channel_rdma_listen_async(ioc, addr, num);
    qio_task_run_in_thread(task, qio_channel_rdma_listen_worker, data,
                           qio_channel_listen_worker_free, context);
}

QIOChannelRDMA *qio_channel_rdma_accept(QIOChannelRDMA *rioc, Error **errp)
{
    QIOChannelRDMA *cioc;

    cioc = qio_channel_rdma_new();
    cioc->remoteAddrLen = sizeof(rioc->remoteAddr);
    cioc->localAddrLen = sizeof(rioc->localAddr);

    trace_qio_channel_rdma_accept(rioc);
retry:
    cioc->fd = raccept(rioc->fd, (struct sockaddr *)&cioc->remoteAddr,
                       &cioc->remoteAddrLen);
    if (cioc->fd < 0) {
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "Unable to accept connection");
        goto error;
    }
    qemu_set_cloexec(cioc->fd);

    if (rgetsockname(cioc->fd, (struct sockaddr *)&cioc->localAddr,
                     &cioc->localAddrLen) < 0) {
        error_setg_errno(errp, errno, "Unable to query local rsocket address");
        goto error;
    }

    trace_qio_channel_rdma_accept_complete(rioc, cioc, cioc->fd);
    return cioc;

error:
    trace_qio_channel_rdma_accept_fail(rioc);
    object_unref(OBJECT(cioc));
    return NULL;
}

static void qio_channel_rdma_init(Object *obj)
{
    QIOChannelRDMA *ioc = QIO_CHANNEL_RDMA(obj);
    ioc->fd = -1;
}

static void qio_channel_rdma_finalize(Object *obj)
{
    QIOChannelRDMA *ioc = QIO_CHANNEL_RDMA(obj);

    if (ioc->fd != -1) {
        rclose(ioc->fd);
        ioc->fd = -1;
    }
}

static ssize_t qio_channel_rdma_readv(QIOChannel *ioc, const struct iovec *iov,
                                      size_t niov, int **fds G_GNUC_UNUSED,
                                      size_t *nfds G_GNUC_UNUSED,
                                      int flags G_GNUC_UNUSED, Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    ssize_t ret;

retry:
    ret = rreadv(rioc->fd, iov, niov);
    if (ret < 0) {
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "Unable to write to rsocket");
        return -1;
    }

    return ret;
}

static ssize_t qio_channel_rdma_writev(QIOChannel *ioc, const struct iovec *iov,
                                       size_t niov, int *fds G_GNUC_UNUSED,
                                       size_t nfds G_GNUC_UNUSED,
                                       int flags G_GNUC_UNUSED, Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    ssize_t ret;

retry:
    ret = rwritev(rioc->fd, iov, niov);
    if (ret <= 0) {
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "Unable to write to rsocket");
        return -1;
    }

    return ret;
}

static void qio_channel_rdma_set_delay(QIOChannel *ioc, bool enabled)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    int v = enabled ? 0 : 1;

    rsetsockopt(rioc->fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
}

static int qio_channel_rdma_close(QIOChannel *ioc, Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);

    if (rioc->fd != -1) {
        rclose(rioc->fd);
        rioc->fd = -1;
    }

    return 0;
}

static int qio_channel_rdma_shutdown(QIOChannel *ioc, QIOChannelShutdown how,
                                     Error **errp)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    int sockhow;

    switch (how) {
    case QIO_CHANNEL_SHUTDOWN_READ:
        sockhow = SHUT_RD;
        break;
    case QIO_CHANNEL_SHUTDOWN_WRITE:
        sockhow = SHUT_WR;
        break;
    case QIO_CHANNEL_SHUTDOWN_BOTH:
    default:
        sockhow = SHUT_RDWR;
        break;
    }

    if (rshutdown(rioc->fd, sockhow) < 0) {
        error_setg_errno(errp, errno, "Unable to shutdown rsocket");
        return -1;
    }

    return 0;
}

static void qio_channel_rdma_class_init(ObjectClass *klass,
                                        void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_rdma_writev;
    ioc_klass->io_readv = qio_channel_rdma_readv;
    ioc_klass->io_close = qio_channel_rdma_close;
    ioc_klass->io_shutdown = qio_channel_rdma_shutdown;
    ioc_klass->io_set_delay = qio_channel_rdma_set_delay;
}

static const TypeInfo qio_channel_rdma_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_RDMA,
    .instance_size = sizeof(QIOChannelRDMA),
    .instance_init = qio_channel_rdma_init,
    .instance_finalize = qio_channel_rdma_finalize,
    .class_init = qio_channel_rdma_class_init,
};

static void qio_channel_rdma_register_types(void)
{
    type_register_static(&qio_channel_rdma_info);
}

type_init(qio_channel_rdma_register_types);
