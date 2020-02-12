/*
 * Sharing QEMU devices via vhost-user protocol
 *
 * Author: Coiby Xu <coiby.xu@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <sys/eventfd.h>
#include "qemu/main-loop.h"
#include "vhost-user-server.h"

static void vmsg_close_fds(VhostUserMsg *vmsg)
{
    int i;
    for (i = 0; i < vmsg->fd_num; i++) {
        close(vmsg->fds[i]);
    }
}

static void vmsg_unblock_fds(VhostUserMsg *vmsg)
{
    int i;
    for (i = 0; i < vmsg->fd_num; i++) {
        qemu_set_nonblock(vmsg->fds[i]);
    }
}


static void close_client(VuClient *client)
{
    vu_deinit(&client->parent);
    client->sioc = NULL;
    object_unref(OBJECT(client->ioc));
    client->closed = true;

}

static void panic_cb(VuDev *vu_dev, const char *buf)
{
    if (buf) {
        error_report("vu_panic: %s", buf);
    }

    VuClient *client = container_of(vu_dev, VuClient, parent);
    VuServer *server = client->server;
    if (!client->closed) {
        close_client(client);
        QTAILQ_REMOVE(&server->clients, client, next);
    }

    if (server->device_panic_notifier) {
        server->device_panic_notifier(client);
    }
}



static bool coroutine_fn
vu_message_read(VuDev *vu_dev, int conn_fd, VhostUserMsg *vmsg)
{
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    int rc, read_bytes = 0;
    /*
     * VhostUserMsg is a packed structure, gcc will complain about passing
     * pointer to a packed structure member if we pass &VhostUserMsg.fd_num
     * and &VhostUserMsg.fds directly when calling qio_channel_readv_full,
     * thus two temporary variables nfds and fds are used here.
     */
    size_t nfds = 0, nfds_t = 0;
    int *fds = NULL, *fds_t = NULL;
    VuClient *client = container_of(vu_dev, VuClient, parent);
    QIOChannel *ioc = client->ioc;

    Error *erp;
    assert(qemu_in_coroutine());
    do {
        /*
         * qio_channel_readv_full may have short reads, keeping calling it
         * until getting VHOST_USER_HDR_SIZE or 0 bytes in total
         */
        rc = qio_channel_readv_full(ioc, &iov, 1, &fds_t, &nfds_t, &erp);
        if (rc < 0) {
            if (rc == QIO_CHANNEL_ERR_BLOCK) {
                qio_channel_yield(ioc, G_IO_IN);
                continue;
            } else {
                error_report("Error while recvmsg: %s", strerror(errno));
                return false;
            }
        }
        read_bytes += rc;
        fds = g_renew(int, fds_t, nfds + nfds_t);
        memcpy(fds + nfds, fds_t, nfds_t);
        nfds += nfds_t;
        if (read_bytes == VHOST_USER_HDR_SIZE || rc == 0) {
            break;
        }
    } while (true);

    vmsg->fd_num = nfds;
    memcpy(vmsg->fds, fds, nfds * sizeof(int));
    g_free(fds);
    /* qio_channel_readv_full will make socket fds blocking, unblock them */
    vmsg_unblock_fds(vmsg);
    if (vmsg->size > sizeof(vmsg->payload)) {
        error_report("Error: too big message request: %d, "
                     "size: vmsg->size: %u, "
                     "while sizeof(vmsg->payload) = %zu",
                     vmsg->request, vmsg->size, sizeof(vmsg->payload));
        goto fail;
    }

    struct iovec iov_payload = {
        .iov_base = (char *)&vmsg->payload,
        .iov_len = vmsg->size,
    };
    if (vmsg->size) {
        rc = qio_channel_readv_all_eof(ioc, &iov_payload, 1, &erp);
        if (rc == -1) {
            error_report("Error while reading: %s", strerror(errno));
            goto fail;
        }
    }

    return true;

fail:
    vmsg_close_fds(vmsg);

    return false;
}


static coroutine_fn void vu_client_next_trip(VuClient *client);

static coroutine_fn void vu_client_trip(void *opaque)
{
    VuClient *client = opaque;

    vu_dispatch(&client->parent);
    client->co_trip = NULL;
    if (!client->closed) {
        vu_client_next_trip(client);
    }
}

static coroutine_fn void vu_client_next_trip(VuClient *client)
{
    if (!client->co_trip) {
        client->co_trip = qemu_coroutine_create(vu_client_trip, client);
        aio_co_schedule(client->ioc->ctx, client->co_trip);
    }
}

static void vu_client_start(VuClient *client)
{
    client->co_trip = qemu_coroutine_create(vu_client_trip, client);
    aio_co_enter(client->ioc->ctx, client->co_trip);
}

static void coroutine_fn vu_kick_cb_next(VuClient *client,
                                          kick_info *data);

static void coroutine_fn vu_kick_cb(void *opaque)
{
    kick_info *data = (kick_info *) opaque;
    int index = data->index;
    VuDev *dev = data->vu_dev;
    VuClient *client;
    client = container_of(dev, VuClient, parent);
    VuVirtq *vq = &dev->vq[index];
    int sock = vq->kick_fd;
    if (sock == -1) {
        return;
    }
    assert(sock == data->fd);
    eventfd_t kick_data;
    ssize_t rc;
    /*
     * When eventfd is closed, the revent is POLLNVAL (=G_IO_NVAL) and
     * reading eventfd will return errno=EBADF (Bad file number).
     * Calling qio_channel_yield(ioc, G_IO_IN) will set reading handler
     * for QIOChannel, but aio_dispatch_handlers will only dispatch
     * G_IO_IN | G_IO_HUP | G_IO_ERR revents while ignoring
     * G_IO_NVAL (POLLNVAL) revents.
     *
     * Thus when eventfd is closed by vhost-user client, QEMU will ignore
     * G_IO_NVAL and keeping polling by repeatedly calling qemu_poll_ns which
     * will lead to 100% CPU usage.
     *
     * To aovid this issue, make sure set_watch and remove_watch use the same
     * AIOContext for QIOChannel. Thus remove_watch will eventually succefully
     * remove eventfd from the set of file descriptors polled for
     * corresponding GSource.
     */
    rc = read(sock, &kick_data, sizeof(eventfd_t));
    if (rc != sizeof(eventfd_t)) {
        if (errno == EAGAIN) {
            qio_channel_yield(data->ioc, G_IO_IN);
        } else if (errno != EINTR) {
            data->co = NULL;
            return;
        }
    } else {
        vq->handler(dev, index);
    }
    data->co = NULL;
    vu_kick_cb_next(client, data);

}

static void coroutine_fn vu_kick_cb_next(VuClient *client,
                                          kick_info *cb_data)
{
    if (!cb_data->co) {
        cb_data->co = qemu_coroutine_create(vu_kick_cb, cb_data);
        aio_co_schedule(client->ioc->ctx, cb_data->co);
    }
}
static const CoIface co_iface = {
    .read_msg = vu_message_read,
    .kick_callback = vu_kick_cb,
};


static void
set_watch(VuDev *vu_dev, int fd, int vu_evt,
          vu_watch_cb_packed_data cb, void *pvt)
{
    /*
     * since aio_dispatch can only pass one user data pointer to the
     * callback function, pack VuDev, pvt into a struct
     */

    VuClient *client;

    client = container_of(vu_dev, VuClient, parent);
    g_assert(vu_dev);
    g_assert(fd >= 0);
    long index = (intptr_t) pvt;
    g_assert(cb);
    kick_info *kick_info = &client->kick_info[index];
    if (!kick_info->co) {
        kick_info->fd = fd;
        QIOChannelFile *fioc = qio_channel_file_new_fd(fd);
        QIOChannel *ioc = QIO_CHANNEL(fioc);
        ioc->ctx = client->ioc->ctx;
        qio_channel_set_blocking(QIO_CHANNEL(ioc), false, NULL);
        kick_info->fioc = fioc;
        kick_info->ioc = ioc;
        kick_info->vu_dev = vu_dev;
        kick_info->co = qemu_coroutine_create(cb, kick_info);
        aio_co_enter(client->ioc->ctx, kick_info->co);
    }
}


static void remove_watch(VuDev *vu_dev, int fd)
{
    VuClient *client;
    int i;
    int index = -1;
    g_assert(vu_dev);
    g_assert(fd >= 0);

    client = container_of(vu_dev, VuClient, parent);
    for (i = 0; i < vu_dev->max_queues; i++) {
        if (client->kick_info[i].fd == fd) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return;
    }

    kick_info *kick_info = &client->kick_info[index];
    if (kick_info->ioc) {
        aio_set_fd_handler(client->ioc->ctx, fd, false, NULL,
                           NULL, NULL, NULL);
        kick_info->ioc = NULL;
        g_free(kick_info->fioc);
        kick_info->co = NULL;
        kick_info->fioc = NULL;
    }
}


static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque)
{
    VuClient *client;
    VuServer *server = opaque;
    client = g_new0(VuClient, 1);

    if (!vu_init_packed_data(&client->parent, server->max_queues,
                             sioc->fd, panic_cb,
                             set_watch, remove_watch,
                             server->vu_iface, &co_iface)) {
        error_report("Failed to initialized libvhost-user");
        g_free(client);
        return;
    }

    client->server = server;
    client->sioc = sioc;
    client->kick_info = g_new0(struct kick_info, server->max_queues);
    /*
     * increase the object reference, so cioc will not freed by
     * qio_net_listener_channel_func which will call object_unref(OBJECT(sioc))
     */
    object_ref(OBJECT(client->sioc));
    qio_channel_set_name(QIO_CHANNEL(sioc), "vhost-user client");
    client->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(client->ioc));
    object_ref(OBJECT(sioc));
    qio_channel_attach_aio_context(client->ioc, server->ctx);
    qio_channel_set_blocking(QIO_CHANNEL(client->sioc), false, NULL);
    client->closed = false;
    QTAILQ_INSERT_TAIL(&server->clients, client, next);
    vu_client_start(client);
}


void vhost_user_server_stop(VuServer *server)
{
    if (!server) {
        return;
    }

    VuClient *client, *next;
    QTAILQ_FOREACH_SAFE(client, &server->clients, next, next) {
        if (!client->closed) {
            close_client(client);
            QTAILQ_REMOVE(&server->clients, client, next);
        }
    }

    if (server->listener) {
        qio_net_listener_disconnect(server->listener);
        object_unref(OBJECT(server->listener));
    }
}

static void detach_context(VuServer *server)
{
    VuClient *client;
    int i;
    QTAILQ_FOREACH(client, &server->clients, next) {
        qio_channel_detach_aio_context(client->ioc);
        for (i = 0; i < client->parent.max_queues; i++) {
            if (client->kick_info[i].ioc) {
                qio_channel_detach_aio_context(client->kick_info[i].ioc);
            }
        }
    }
}

static void attach_context(VuServer *server, AioContext *ctx)
{
    VuClient *client;
    int i;
    QTAILQ_FOREACH(client, &server->clients, next) {
        qio_channel_attach_aio_context(client->ioc, ctx);
        if (client->co_trip) {
            aio_co_schedule(ctx, client->co_trip);
        }
        for (i = 0; i < client->parent.max_queues; i++) {
            if (client->kick_info[i].co) {
                qio_channel_attach_aio_context(client->kick_info[i].ioc, ctx);
                aio_co_schedule(ctx, client->kick_info[i].co);
            }
        }
    }
}
void change_vu_context(AioContext *ctx, VuServer *server)
{
    AioContext *acquire_ctx = ctx ? ctx : server->ctx;
    aio_context_acquire(acquire_ctx);
    server->ctx = ctx ? ctx : qemu_get_aio_context();
    if (ctx) {
        attach_context(server, ctx);
    } else {
        detach_context(server);
    }
    aio_context_release(acquire_ctx);
}


VuServer *vhost_user_server_start(uint16_t max_queues,
                                  char *unix_socket,
                                  AioContext *ctx,
                                  void *server_ptr,
                                  void *device_panic_notifier,
                                  const VuDevIface *vu_iface,
                                  Error **errp)
{
    VuServer *server = g_new0(VuServer, 1);
    server->ptr_in_device = server_ptr;
    server->listener = qio_net_listener_new();
    SocketAddress addr = {};
    addr.u.q_unix.path = (char *) unix_socket;
    addr.type = SOCKET_ADDRESS_TYPE_UNIX;
    if (qio_net_listener_open_sync(server->listener, &addr, 1, errp) < 0) {
        goto error;
    }

    qio_net_listener_set_name(server->listener, "vhost-user-backend-listener");

    server->vu_iface = vu_iface;
    server->max_queues = max_queues;
    server->ctx = ctx;
    qio_net_listener_set_client_func(server->listener,
                                     vu_accept,
                                     server,
                                     NULL);

    QTAILQ_INIT(&server->clients);
    return server;
error:
    g_free(server);
    return NULL;
}
