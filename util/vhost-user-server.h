#include "contrib/libvhost-user/libvhost-user.h"
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/net-listener.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_blk.h"

typedef struct VuClientInfo VuClientInfo;

typedef struct VuServer {
    QIONetListener *listener;
    AioContext *ctx;
    QTAILQ_HEAD(, VuClientInfo) clients;
    void (*device_panic_notifier)(struct VuClientInfo *client) ;
    int max_queues;
    const VuDevIface *vu_iface;
    /*
     * @ptr_in_device: VuServer pointer memory location in vhost-user device
     * struct, so later container_of can be used to get device destruct
     */
    void *ptr_in_device;
    bool close;
} VuServer;

typedef struct KickInfo {
    VuDev *vu_dev;
    int fd; /*kick fd*/
    long index; /*queue index*/
    vu_watch_cb cb;
} KickInfo;

struct VuClientInfo {
    VuDev vu_dev;
    VuServer *server;
    QIOChannel *ioc; /* The current I/O channel */
    QIOChannelSocket *sioc; /* The underlying data channel */
    Coroutine *co_trip; /* coroutine for processing VhostUserMsg */
    KickInfo *kick_info; /* an array with the length of the queue number */
    QTAILQ_ENTRY(VuClientInfo) next;
    /* restart coroutine co_trip if AIOContext is changed */
    bool aio_context_changed;
    bool closed;
};


VuServer *vhost_user_server_start(uint16_t max_queues,
                                  SocketAddress *unix_socket,
                                  AioContext *ctx,
                                  void *server_ptr,
                                  void *device_panic_notifier,
                                  const VuDevIface *vu_iface,
                                  Error **errp);

void vhost_user_server_stop(VuServer *server);

void vhost_user_server_set_aio_context(AioContext *ctx, VuServer *server);
