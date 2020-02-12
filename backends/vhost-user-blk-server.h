#include "util/vhost-user-server.h"
typedef struct VuBlockDev VuBlockDev;
#define TYPE_VHOST_USER_BLK_SERVER "vhost-user-blk-server"
#define VHOST_USER_BLK_SERVER(obj) \
   OBJECT_CHECK(VuBlockDev, obj, TYPE_VHOST_USER_BLK_SERVER)

/* vhost user block device */
struct VuBlockDev {
    Object parent_obj;
    char *node_name;
    char *unix_socket;
    bool exit_when_panic;
    AioContext *ctx;
    VuServer *vu_server;
    uint32_t blk_size;
    BlockBackend *backend;
    QIOChannelSocket *sioc;
    QTAILQ_ENTRY(VuBlockDev) next;
    struct virtio_blk_config blkcfg;
    bool writable;
};
