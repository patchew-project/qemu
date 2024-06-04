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
#include "io/channel-util.h"
#include "io/channel-watch.h"
#include "io/channel.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qemu/atomic.h"
#include "qemu/error-report.h"
#include "qemu/thread.h"
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

typedef enum {
    CLEAR_POLLIN,
    CLEAR_POLLOUT,
    SET_POLLIN,
    SET_POLLOUT,
} UpdateEvent;

typedef enum {
    RP_CMD_ADD_IOC,
    RP_CMD_DEL_IOC,
    RP_CMD_UPDATE,
} RpollerCMD;

typedef struct {
    RpollerCMD cmd;
    QIOChannelRDMA *rioc;
} RpollerMsg;

/*
 * rpoll() on the rsocket fd with rpoll_events, when POLLIN/POLLOUT event
 * occurs, it will write/read the pollin_eventfd/pollout_eventfd to allow
 * qemu g_poll/ppoll() get the POLLIN/POLLOUT event
 */
static struct Rpoller {
    QemuThread thread;
    bool is_running;
    int sock[2];
    int count; /* the number of rsocket fds being rpoll() */
    int size; /* the size of fds/riocs */
    struct pollfd *fds;
    QIOChannelRDMA **riocs;
} rpoller;

static void qio_channel_rdma_notify_rpoller(QIOChannelRDMA *rioc,
                                            RpollerCMD cmd)
{
    RpollerMsg msg;
    int ret;

    msg.cmd = cmd;
    msg.rioc = rioc;

    ret = RETRY_ON_EINTR(write(rpoller.sock[0], &msg, sizeof msg));
    if (ret != sizeof msg) {
        error_report("%s: failed to send msg, errno: %d", __func__, errno);
    }
}

static void qio_channel_rdma_update_poll_event(QIOChannelRDMA *rioc,
                                               UpdateEvent action,
                                               bool notify_rpoller)
{
    /* An eventfd with the value of ULLONG_MAX - 1 is readable but unwritable */
    unsigned long long buf = ULLONG_MAX - 1;

    switch (action) {
    /* only rpoller do SET_* action, to allow qemu ppoll() get the event */
    case SET_POLLIN:
        RETRY_ON_EINTR(write(rioc->pollin_eventfd, &buf, sizeof buf));
        rioc->rpoll_events &= ~POLLIN;
        break;
    case SET_POLLOUT:
        RETRY_ON_EINTR(read(rioc->pollout_eventfd, &buf, sizeof buf));
        rioc->rpoll_events &= ~POLLOUT;
        break;

    /* the rsocket fd is not ready to rread/rwrite */
    case CLEAR_POLLIN:
        RETRY_ON_EINTR(read(rioc->pollin_eventfd, &buf, sizeof buf));
        rioc->rpoll_events |= POLLIN;
        break;
    case CLEAR_POLLOUT:
        RETRY_ON_EINTR(write(rioc->pollout_eventfd, &buf, sizeof buf));
        rioc->rpoll_events |= POLLOUT;
        break;
    default:
        break;
    }

    /* notify rpoller to rpoll() POLLIN/POLLOUT events */
    if (notify_rpoller) {
        qio_channel_rdma_notify_rpoller(rioc, RP_CMD_UPDATE);
    }
}

static void qio_channel_rdma_rpoller_add_rioc(QIOChannelRDMA *rioc)
{
    if (rioc->index != -1) {
        error_report("%s: rioc already exsits", __func__);
        return;
    }

    rioc->index = ++rpoller.count;

    if (rpoller.count + 1 > rpoller.size) {
        rpoller.size *= 2;
        rpoller.fds = g_renew(struct pollfd, rpoller.fds, rpoller.size);
        rpoller.riocs = g_renew(QIOChannelRDMA *, rpoller.riocs, rpoller.size);
    }

    rpoller.fds[rioc->index].fd = rioc->fd;
    rpoller.fds[rioc->index].events = rioc->rpoll_events;
    rpoller.riocs[rioc->index] = rioc;
}

static void qio_channel_rdma_rpoller_del_rioc(QIOChannelRDMA *rioc)
{
    if (rioc->index == -1) {
        error_report("%s: rioc not exsits", __func__);
        return;
    }

    rpoller.fds[rioc->index] = rpoller.fds[rpoller.count];
    rpoller.riocs[rioc->index] = rpoller.riocs[rpoller.count];
    rpoller.riocs[rioc->index]->index = rioc->index;
    rpoller.count--;

    close(rioc->pollin_eventfd);
    close(rioc->pollout_eventfd);
    rioc->index = -1;
    rioc->rpoll_events = 0;
}

static void qio_channel_rdma_rpoller_update_ioc(QIOChannelRDMA *rioc)
{
    if (rioc->index == -1) {
        error_report("%s: rioc not exsits", __func__);
        return;
    }

    rpoller.fds[rioc->index].fd = rioc->fd;
    rpoller.fds[rioc->index].events = rioc->rpoll_events;
}

static void qio_channel_rdma_rpoller_process_msg(void)
{
    RpollerMsg msg;
    int ret;

    ret = RETRY_ON_EINTR(read(rpoller.sock[1], &msg, sizeof msg));
    if (ret != sizeof msg) {
        error_report("%s: rpoller failed to recv msg: %s", __func__,
                     strerror(errno));
        return;
    }

    switch (msg.cmd) {
    case RP_CMD_ADD_IOC:
        qio_channel_rdma_rpoller_add_rioc(msg.rioc);
        break;
    case RP_CMD_DEL_IOC:
        qio_channel_rdma_rpoller_del_rioc(msg.rioc);
        break;
    case RP_CMD_UPDATE:
        qio_channel_rdma_rpoller_update_ioc(msg.rioc);
        break;
    default:
        break;
    }
}

static void qio_channel_rdma_rpoller_cleanup(void)
{
    close(rpoller.sock[0]);
    close(rpoller.sock[1]);
    rpoller.sock[0] = -1;
    rpoller.sock[1] = -1;
    g_free(rpoller.fds);
    g_free(rpoller.riocs);
    rpoller.fds = NULL;
    rpoller.riocs = NULL;
    rpoller.count = 0;
    rpoller.size = 0;
    rpoller.is_running = false;
}

static void *qio_channel_rdma_rpoller_thread(void *opaque)
{
    int i, ret, error_events = POLLERR | POLLHUP | POLLNVAL;

    do {
        ret = rpoll(rpoller.fds, rpoller.count + 1, -1);
        if (ret < 0 && errno != -EINTR) {
            error_report("%s: rpoll() error: %s", __func__, strerror(errno));
            break;
        }

        for (i = 1; i <= rpoller.count; i++) {
            if (rpoller.fds[i].revents & (POLLIN | error_events)) {
                qio_channel_rdma_update_poll_event(rpoller.riocs[i], SET_POLLIN,
                                                   false);
                rpoller.fds[i].events &= ~POLLIN;
            }
            if (rpoller.fds[i].revents & (POLLOUT | error_events)) {
                qio_channel_rdma_update_poll_event(rpoller.riocs[i],
                                                   SET_POLLOUT, false);
                rpoller.fds[i].events &= ~POLLOUT;
            }
            /* ignore this fd */
            if (rpoller.fds[i].revents & (error_events)) {
                rpoller.fds[i].fd = -1;
            }
        }

        if (rpoller.fds[0].revents) {
            qio_channel_rdma_rpoller_process_msg();
        }
    } while (rpoller.count >= 1);

    qio_channel_rdma_rpoller_cleanup();

    return NULL;
}

static void qio_channel_rdma_rpoller_start(void)
{
    if (qatomic_xchg(&rpoller.is_running, true)) {
        return;
    }

    if (qemu_socketpair(AF_UNIX, SOCK_STREAM, 0, rpoller.sock)) {
        rpoller.is_running = false;
        error_report("%s: failed to create socketpair %s", __func__,
                     strerror(errno));
        return;
    }

    rpoller.count = 0;
    rpoller.size = 4;
    rpoller.fds = g_malloc0_n(rpoller.size, sizeof(struct pollfd));
    rpoller.riocs = g_malloc0_n(rpoller.size, sizeof(QIOChannelRDMA *));
    rpoller.fds[0].fd = rpoller.sock[1];
    rpoller.fds[0].events = POLLIN;

    qemu_thread_create(&rpoller.thread, "qio-channel-rdma-rpoller",
                       qio_channel_rdma_rpoller_thread, NULL,
                       QEMU_THREAD_JOINABLE);
}

static void qio_channel_rdma_add_rioc_to_rpoller(QIOChannelRDMA *rioc)
{
    int flags = EFD_CLOEXEC | EFD_NONBLOCK;

    /*
     * A single eventfd is either readable or writable. A single eventfd cannot
     * represent a state where it is neither readable nor writable. so use two
     * eventfds here.
     */
    rioc->pollin_eventfd = eventfd(0, flags);
    rioc->pollout_eventfd = eventfd(0, flags);
    /* pollout_eventfd with the value 0, means writable, make it unwritable */
    qio_channel_rdma_update_poll_event(rioc, CLEAR_POLLOUT, false);

    /* tell the rpoller to rpoll() events on rioc->socketfd */
    rioc->rpoll_events = POLLIN | POLLOUT;
    qio_channel_rdma_notify_rpoller(rioc, RP_CMD_ADD_IOC);
}

QIOChannelRDMA *qio_channel_rdma_new(void)
{
    QIOChannelRDMA *rioc;
    QIOChannel *ioc;

    qio_channel_rdma_rpoller_start();
    if (!rpoller.is_running) {
        return NULL;
    }

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

    qio_channel_rdma_add_rioc_to_rpoller(rioc);

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

    qio_channel_rdma_add_rioc_to_rpoller(rioc);

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

QIOChannelRDMA *coroutine_mixed_fn qio_channel_rdma_accept(QIOChannelRDMA *rioc,
                                                           Error **errp)
{
    QIOChannel *ioc = QIO_CHANNEL(rioc);
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
        if (errno == EAGAIN) {
            if (!(rioc->rpoll_events & POLLIN)) {
                qio_channel_rdma_update_poll_event(rioc, CLEAR_POLLIN, true);
            }
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_IN);
            } else {
                qio_channel_wait(ioc, G_IO_IN);
            }
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

    qio_channel_rdma_add_rioc_to_rpoller(cioc);

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
    ioc->pollin_eventfd = -1;
    ioc->pollout_eventfd = -1;
    ioc->index = -1;
    ioc->rpoll_events = 0;
}

static void qio_channel_rdma_finalize(Object *obj)
{
    QIOChannelRDMA *ioc = QIO_CHANNEL_RDMA(obj);

    if (ioc->fd != -1) {
        qio_channel_rdma_notify_rpoller(ioc, RP_CMD_DEL_IOC);
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
        if (errno == EAGAIN) {
            if (!(rioc->rpoll_events & POLLIN)) {
                qio_channel_rdma_update_poll_event(rioc, CLEAR_POLLIN, true);
            }
            return QIO_CHANNEL_ERR_BLOCK;
        }
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
        if (errno == EAGAIN) {
            if (!(rioc->rpoll_events & POLLOUT)) {
                qio_channel_rdma_update_poll_event(rioc, CLEAR_POLLOUT, true);
            }
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "Unable to write to rsocket");
        return -1;
    }

    return ret;
}

static int qio_channel_rdma_set_blocking(QIOChannel *ioc, bool enabled,
                                         Error **errp G_GNUC_UNUSED)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);
    int flags, ret;

    flags = rfcntl(rioc->fd, F_GETFL);
    if (enabled) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }

    ret = rfcntl(rioc->fd, F_SETFL, flags);
    if (ret) {
        error_setg_errno(errp, errno,
                         "Unable to rfcntl rsocket fd with flags %d", flags);
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
        qio_channel_rdma_notify_rpoller(rioc, RP_CMD_DEL_IOC);
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

static void
qio_channel_rdma_set_aio_fd_handler(QIOChannel *ioc, AioContext *read_ctx,
                                    IOHandler *io_read, AioContext *write_ctx,
                                    IOHandler *io_write, void *opaque)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);

    qio_channel_util_set_aio_fd_handler(rioc->pollin_eventfd, read_ctx, io_read,
                                        rioc->pollout_eventfd, write_ctx,
                                        io_write, opaque);
}

static GSource *qio_channel_rdma_create_watch(QIOChannel *ioc,
                                              GIOCondition condition)
{
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(ioc);

    switch (condition) {
    case G_IO_IN:
        return qio_channel_create_fd_watch(ioc, rioc->pollin_eventfd,
                                           condition);
    case G_IO_OUT:
        return qio_channel_create_fd_watch(ioc, rioc->pollout_eventfd,
                                           condition);
    default:
        error_report("%s: do not support watch 0x%x event", __func__,
                     condition);
        return NULL;
    }
}

static void qio_channel_rdma_class_init(ObjectClass *klass,
                                        void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_rdma_writev;
    ioc_klass->io_readv = qio_channel_rdma_readv;
    ioc_klass->io_set_blocking = qio_channel_rdma_set_blocking;
    ioc_klass->io_close = qio_channel_rdma_close;
    ioc_klass->io_shutdown = qio_channel_rdma_shutdown;
    ioc_klass->io_set_delay = qio_channel_rdma_set_delay;
    ioc_klass->io_create_watch = qio_channel_rdma_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_rdma_set_aio_fd_handler;
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
