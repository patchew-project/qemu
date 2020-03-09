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


static void close_client(VuClientInfo *client)
{
    vu_deinit(&client->vu_dev);
    client->sioc = NULL;
    object_unref(OBJECT(client->ioc));
    client->closed = true;

}

static void panic_cb(VuDev *vu_dev, const char *buf)
{
    VuClientInfo *client = container_of(vu_dev, VuClientInfo, vu_dev);
    VuServer *server = client->server;

    if (buf) {
        error_report("vu_panic: %s", buf);
    }

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
     * Store fds/nfds returned from qio_channel_readv_full into
     * temporary variables.
     *
     * VhostUserMsg is a packed structure, gcc will complain about passing
     * pointer to a packed structure member if we pass &VhostUserMsg.fd_num
     * and &VhostUserMsg.fds directly when calling qio_channel_readv_full,
     * thus two temporary variables nfds and fds are used here.
     */
    size_t nfds = 0, nfds_t = 0;
    int *fds = NULL, *fds_t = NULL;
    VuClientInfo *client = container_of(vu_dev, VuClientInfo, vu_dev);
    QIOChannel *ioc = client->ioc;

    Error *local_err = NULL;
    assert(qemu_in_coroutine());
    do {
        /*
         * qio_channel_readv_full may have short reads, keeping calling it
         * until getting VHOST_USER_HDR_SIZE or 0 bytes in total
         */
        rc = qio_channel_readv_full(ioc, &iov, 1, &fds_t, &nfds_t, &local_err);
        if (rc < 0) {
            if (rc == QIO_CHANNEL_ERR_BLOCK) {
                qio_channel_yield(ioc, G_IO_IN);
                continue;
            } else {
                error_report_err(local_err);
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
    assert(nfds <= VHOST_MEMORY_MAX_NREGIONS);
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
        rc = qio_channel_readv_all_eof(ioc, &iov_payload, 1, &local_err);
        if (rc == -1) {
            error_report_err(local_err);
            goto fail;
        }
    }

    return true;

fail:
    vmsg_close_fds(vmsg);

    return false;
}


static void vu_client_start(VuClientInfo *client);
static coroutine_fn void vu_client_trip(void *opaque)
{
    VuClientInfo *client = opaque;

    while (!client->aio_context_changed && !client->closed) {
        vu_dispatch(&client->vu_dev);
    }

    if (client->aio_context_changed) {
        client->aio_context_changed = false;
        vu_client_start(client);
    }
}

static void vu_client_start(VuClientInfo *client)
{
    client->co_trip = qemu_coroutine_create(vu_client_trip, client);
    aio_co_enter(client->ioc->ctx, client->co_trip);
}

/*
 * a wrapper for vu_kick_cb
 *
 * since aio_dispatch can only pass one user data pointer to the
 * callback function, pack VuDev and pvt into a struct. Then unpack it
 * and pass them to vu_kick_cb
 */
static void kick_handler(void *opaque)
{
    KickInfo *kick_info = opaque;
    kick_info->cb(kick_info->vu_dev, 0, (void *) kick_info->index);
}


static void
set_watch(VuDev *vu_dev, int fd, int vu_evt,
          vu_watch_cb cb, void *pvt)
{

    VuClientInfo *client;
    client = container_of(vu_dev, VuClientInfo, vu_dev);
    g_assert(vu_dev);
    g_assert(fd >= 0);
    long index = (intptr_t) pvt;
    g_assert(cb);
    KickInfo *kick_info = &client->kick_info[index];
    if (!kick_info->cb) {
        kick_info->fd = fd;
        kick_info->cb = cb;
        qemu_set_nonblock(fd);
        aio_set_fd_handler(client->ioc->ctx, fd, false, kick_handler,
                           NULL, NULL, kick_info);
        kick_info->vu_dev = vu_dev;
    }
}


static void remove_watch(VuDev *vu_dev, int fd)
{
    VuClientInfo *client;
    int i;
    int index = -1;
    g_assert(vu_dev);
    g_assert(fd >= 0);

    client = container_of(vu_dev, VuClientInfo, vu_dev);
    for (i = 0; i < vu_dev->max_queues; i++) {
        if (client->kick_info[i].fd == fd) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return;
    }
    client->kick_info[i].cb = NULL;
    aio_set_fd_handler(client->ioc->ctx, fd, false, NULL, NULL, NULL, NULL);
}


static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque)
{
    VuClientInfo *client;
    VuServer *server = opaque;
    client = g_new0(VuClientInfo, 1);
    if (!vu_init(&client->vu_dev, server->max_queues, sioc->fd, panic_cb,
                 vu_message_read, set_watch, remove_watch, server->vu_iface)) {
        error_report("Failed to initialized libvhost-user");
        g_free(client);
        return;
    }

    client->server = server;
    client->sioc = sioc;
    client->kick_info = g_new0(KickInfo, server->max_queues);
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

    VuClientInfo *client, *next;
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
    VuClientInfo *client;
    int i;
    QTAILQ_FOREACH(client, &server->clients, next) {
        qio_channel_detach_aio_context(client->ioc);
        AioContext *ctx = client->ioc->ctx;
        for (i = 0; i < client->vu_dev.max_queues; i++) {
            if (client->kick_info[i].cb) {
                aio_set_fd_handler(ctx, client->kick_info[i].fd, false, NULL,
                                   NULL, NULL, NULL);
            }
        }
    }
}

static void attach_context(VuServer *server, AioContext *ctx)
{
    VuClientInfo *client;
    int i;
    QTAILQ_FOREACH(client, &server->clients, next) {
        qio_channel_attach_aio_context(client->ioc, ctx);
        client->aio_context_changed = true;
        if (client->co_trip) {
            aio_co_schedule(ctx, client->co_trip);
        }
        for (i = 0; i < client->vu_dev.max_queues; i++) {
            if (client->kick_info[i].cb) {
                aio_set_fd_handler(ctx, client->kick_info[i].fd, false,
                                   kick_handler, NULL, NULL,
                                   &client->kick_info[i]);
            }
        }
    }
}

void vhost_user_server_set_aio_context(AioContext *ctx, VuServer *server)
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
                                  SocketAddress *socket_addr,
                                  AioContext *ctx,
                                  void *server_ptr,
                                  void *device_panic_notifier,
                                  const VuDevIface *vu_iface,
                                  Error **errp)
{
    VuServer *server = g_new0(VuServer, 1);
    server->ptr_in_device = server_ptr;
    server->listener = qio_net_listener_new();
    if (qio_net_listener_open_sync(server->listener, socket_addr, 1,
                                   errp) < 0) {
        goto error;
    }

    qio_net_listener_set_name(server->listener, "vhost-user-backend-listener");

    server->vu_iface = vu_iface;
    server->max_queues = max_queues;
    server->ctx = ctx;
    server->device_panic_notifier = device_panic_notifier;
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
