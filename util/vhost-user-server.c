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

static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque);

static void close_client(VuServer *server)
{
    vu_deinit(&server->vu_dev);
    object_unref(OBJECT(server->sioc));
    object_unref(OBJECT(server->ioc));
    server->sioc_slave = NULL;
    object_unref(OBJECT(server->ioc_slave));
    /*
     * Set the callback function for network listener so another
     * vhost-user client can connect to this server
     */
    qio_net_listener_set_client_func(server->listener,
                                     vu_accept,
                                     server,
                                     NULL);
}

static void panic_cb(VuDev *vu_dev, const char *buf)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);

    if (buf) {
        error_report("vu_panic: %s", buf);
    }

    if (server->sioc) {
        close_client(server);
        server->sioc = NULL;
    }

    if (server->device_panic_notifier) {
        server->device_panic_notifier(server);
    }
}

static QIOChannel *slave_io_channel(VuServer *server, int fd,
                                    Error **local_err)
{
    if (server->sioc_slave) {
        if (fd == server->sioc_slave->fd) {
            return server->ioc_slave;
        }
    } else {
        server->sioc_slave = qio_channel_socket_new_fd(fd, local_err);
        if (!*local_err) {
            server->ioc_slave = QIO_CHANNEL(server->sioc_slave);
            return server->ioc_slave;
        }
    }

    return NULL;
}

static bool coroutine_fn
vu_message_read(VuDev *vu_dev, int conn_fd, VhostUserMsg *vmsg)
{
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    int rc, read_bytes = 0;
    Error *local_err = NULL;
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
    int *fds_t = NULL;
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    QIOChannel *ioc = NULL;

    if (conn_fd == server->sioc->fd) {
        ioc = server->ioc;
    } else {
        /* Slave communication will also use this function to read msg */
        ioc = slave_io_channel(server, conn_fd, &local_err);
    }

    if (!ioc) {
        error_report_err(local_err);
        goto fail;
    }

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
        if (nfds_t > 0) {
            if (nfds + nfds_t > G_N_ELEMENTS(vmsg->fds)) {
                error_report("A maximum of %d fds are allowed, "
                             "however got %lu fds now",
                             VHOST_MEMORY_MAX_NREGIONS, nfds + nfds_t);
                goto fail;
            }
            memcpy(vmsg->fds + nfds, fds_t,
                   nfds_t *sizeof(vmsg->fds[0]));
            nfds += nfds_t;
            g_free(fds_t);
        }
        if (read_bytes == VHOST_USER_HDR_SIZE || rc == 0) {
            break;
        }
        iov.iov_base = (char *)vmsg + read_bytes;
        iov.iov_len = VHOST_USER_HDR_SIZE - read_bytes;
    } while (true);

    vmsg->fd_num = nfds;
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


static void vu_client_start(VuServer *server);
static coroutine_fn void vu_client_trip(void *opaque)
{
    VuServer *server = opaque;

    while (!server->aio_context_changed && server->sioc) {
        vu_dispatch(&server->vu_dev);
    }

    if (server->aio_context_changed && server->sioc) {
        server->aio_context_changed = false;
        vu_client_start(server);
    }
}

static void vu_client_start(VuServer *server)
{
    server->co_trip = qemu_coroutine_create(vu_client_trip, server);
    aio_co_enter(server->ctx, server->co_trip);
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

    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    g_assert(vu_dev);
    g_assert(fd >= 0);
    long index = (intptr_t) pvt;
    g_assert(cb);
    KickInfo *kick_info = &server->kick_info[index];
    if (!kick_info->cb) {
        kick_info->fd = fd;
        kick_info->cb = cb;
        qemu_set_nonblock(fd);
        aio_set_fd_handler(server->ioc->ctx, fd, false, kick_handler,
                           NULL, NULL, kick_info);
        kick_info->vu_dev = vu_dev;
    }
}


static void remove_watch(VuDev *vu_dev, int fd)
{
    VuServer *server;
    int i;
    int index = -1;
    g_assert(vu_dev);
    g_assert(fd >= 0);

    server = container_of(vu_dev, VuServer, vu_dev);
    for (i = 0; i < vu_dev->max_queues; i++) {
        if (server->kick_info[i].fd == fd) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return;
    }
    server->kick_info[i].cb = NULL;
    aio_set_fd_handler(server->ioc->ctx, fd, false, NULL, NULL, NULL, NULL);
}


static void vu_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                      gpointer opaque)
{
    VuServer *server = opaque;

    if (server->sioc) {
        warn_report("Only one vhost-user client is allowed to "
                    "connect the server one time");
        return;
    }

    if (!vu_init(&server->vu_dev, server->max_queues, sioc->fd, panic_cb,
                 vu_message_read, set_watch, remove_watch, server->vu_iface)) {
        error_report("Failed to initialized libvhost-user");
        return;
    }

    /*
     * Unset the callback function for network listener to make another
     * vhost-user client keeping waiting until this client disconnects
     */
    qio_net_listener_set_client_func(server->listener,
                                     NULL,
                                     NULL,
                                     NULL);
    server->sioc = sioc;
    server->kick_info = g_new0(KickInfo, server->max_queues);
    /*
     * Increase the object reference, so sioc will not freed by
     * qio_net_listener_channel_func which will call object_unref(OBJECT(sioc))
     */
    object_ref(OBJECT(server->sioc));
    qio_channel_set_name(QIO_CHANNEL(sioc), "vhost-user client");
    server->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(server->ioc));
    qio_channel_attach_aio_context(server->ioc, server->ctx);
    qio_channel_set_blocking(QIO_CHANNEL(server->sioc), false, NULL);
    vu_client_start(server);
}


void vhost_user_server_stop(VuServer *server)
{
    if (!server) {
        return;
    }

    if (server->sioc) {
        close_client(server);
        object_unref(OBJECT(server->sioc));
    }

    if (server->listener) {
        qio_net_listener_disconnect(server->listener);
        object_unref(OBJECT(server->listener));
    }

    g_free(server->kick_info);
}

static void detach_context(VuServer *server)
{
    int i;
    AioContext *ctx = server->ioc->ctx;
    qio_channel_detach_aio_context(server->ioc);
    for (i = 0; i < server->vu_dev.max_queues; i++) {
        if (server->kick_info[i].cb) {
            aio_set_fd_handler(ctx, server->kick_info[i].fd, false, NULL,
                               NULL, NULL, NULL);
        }
    }
}

static void attach_context(VuServer *server, AioContext *ctx)
{
    int i;
    qio_channel_attach_aio_context(server->ioc, ctx);
    server->aio_context_changed = true;
    if (server->co_trip) {
        aio_co_schedule(ctx, server->co_trip);
    }
    for (i = 0; i < server->vu_dev.max_queues; i++) {
        if (server->kick_info[i].cb) {
            aio_set_fd_handler(ctx, server->kick_info[i].fd, false,
                               kick_handler, NULL, NULL,
                               &server->kick_info[i]);
        }
    }
}

void vhost_user_server_set_aio_context(AioContext *ctx, VuServer *server)
{
    server->ctx = ctx ? ctx : qemu_get_aio_context();
    if (!server->sioc) {
        return;
    }
    if (ctx) {
        attach_context(server, ctx);
    } else {
        detach_context(server);
    }
}


bool vhost_user_server_start(VuServer *server,
                             SocketAddress *socket_addr,
                             AioContext *ctx,
                             uint16_t max_queues,
                             DevicePanicNotifierFn *device_panic_notifier,
                             const VuDevIface *vu_iface,
                             Error **errp)
{
    server->listener = qio_net_listener_new();
    if (qio_net_listener_open_sync(server->listener, socket_addr, 1,
                                   errp) < 0) {
        return false;
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

    return true;
}
