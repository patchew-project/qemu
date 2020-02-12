#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/net-listener.h"
#include "contrib/libvhost-user/libvhost-user.h"
#include "standard-headers/linux/virtio_blk.h"
#include "qemu/error-report.h"

typedef struct VuClient VuClient;

typedef struct VuServer {
    QIONetListener *listener;
    AioContext *ctx;
    QTAILQ_HEAD(, VuClient) clients;
    void (*device_panic_notifier)(struct VuClient *client) ;
    int max_queues;
    const VuDevIface *vu_iface;
    /*
     * @ptr_in_device: VuServer pointer memory location in vhost-user device
     * struct, so later container_of can be used to get device destruct
     */
    void *ptr_in_device;
    bool close;
} VuServer;

typedef struct kick_info {
    VuDev *vu_dev;
    int fd; /*kick fd*/
    long index; /*queue index*/
    QIOChannel *ioc; /*I/O channel for kick fd*/
    QIOChannelFile *fioc; /*underlying data channel for kick fd*/
    Coroutine *co;
} kick_info;

struct VuClient {
    VuDev parent;
    VuServer *server;
    QIOChannel *ioc; /* The current I/O channel */
    QIOChannelSocket *sioc; /* The underlying data channel */
    Coroutine *co_trip;
    struct kick_info *kick_info;
    QTAILQ_ENTRY(VuClient) next;
    bool closed;
};


VuServer *vhost_user_server_start(uint16_t max_queues,
                                  char *unix_socket,
                                  AioContext *ctx,
                                  void *server_ptr,
                                  void *device_panic_notifier,
                                  const VuDevIface *vu_iface,
                                  Error **errp);

void vhost_user_server_stop(VuServer *server);

void change_vu_context(AioContext *ctx, VuServer *server);
