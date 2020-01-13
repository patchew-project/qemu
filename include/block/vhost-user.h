#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "contrib/libvhost-user/libvhost-user.h"
#include "standard-headers/linux/virtio_blk.h"
typedef struct VubDev VubDev;
typedef struct VuClient VuClient;
#define TYPE_VHOST_USER_SERVER "vhost-user-server"

#define VHOST_USER_SERVER(obj) \
   OBJECT_CHECK(VubDev, obj, TYPE_VHOST_USER_SERVER)
/* vhost user block device */
struct VubDev {
    Object parent_obj;
    char *name;
    char *unix_socket;
    bool exit_panic;
    bool close;
    BlockBackend *backend;
    AioContext *ctx;
    QIONetListener *listener;
    QIOChannelSocket *sioc;
    QTAILQ_HEAD(, VuClient) clients;
    QTAILQ_ENTRY(VubDev) next;
    struct virtio_blk_config blkcfg;
    bool writable;
};

struct VuClient {
    VuDev parent;
    int refcount;
    VubDev *blk;
    QIOChannelSocket *sioc; /* The underlying data channel */
    QIOChannel *ioc; /* The current I/O channel */
    QTAILQ_ENTRY(VuClient) next;
    bool closed;
};
VubDev *vub_dev_find(const char *name);

void vhost_user_server_free(VubDev *vub_device, bool called_by_QOM);
void vub_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                gpointer opaque);

void vub_free(VubDev *vub_dev, bool called_by_QOM);

void vub_initialize_config(BlockDriverState *bs,
                           struct virtio_blk_config *config);
