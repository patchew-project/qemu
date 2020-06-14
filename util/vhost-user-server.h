/*
 * Sharing QEMU devices via vhost-user protocol
 *
 * Author: Coiby Xu <coiby.xu@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VHOST_USER_SERVER_H
#define VHOST_USER_SERVER_H

#include "contrib/libvhost-user/libvhost-user.h"
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/net-listener.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_blk.h"

typedef struct KickInfo {
    VuDev *vu_dev;
    int fd; /*kick fd*/
    long index; /*queue index*/
    vu_watch_cb cb;
} KickInfo;

typedef struct VuServer {
    QIONetListener *listener;
    AioContext *ctx;
    void (*device_panic_notifier)(struct VuServer *server) ;
    int max_queues;
    const VuDevIface *vu_iface;
    VuDev vu_dev;
    QIOChannel *ioc; /* The I/O channel with the client */
    QIOChannelSocket *sioc; /* The underlying data channel with the client */
    /* IOChannel for fd provided via VHOST_USER_SET_SLAVE_REQ_FD */
    QIOChannel *ioc_slave;
    QIOChannelSocket *sioc_slave;
    Coroutine *co_trip; /* coroutine for processing VhostUserMsg */
    KickInfo *kick_info; /* an array with the length of the queue number */
    /* restart coroutine co_trip if AIOContext is changed */
    bool aio_context_changed;
} VuServer;


typedef void DevicePanicNotifierFn(struct VuServer *server);

bool vhost_user_server_start(VuServer *server,
                             SocketAddress *unix_socket,
                             AioContext *ctx,
                             uint16_t max_queues,
                             DevicePanicNotifierFn *device_panic_notifier,
                             const VuDevIface *vu_iface,
                             Error **errp);

void vhost_user_server_stop(VuServer *server);

void vhost_user_server_set_aio_context(AioContext *ctx, VuServer *server);

#endif /* VHOST_USER_SERVER_H */
