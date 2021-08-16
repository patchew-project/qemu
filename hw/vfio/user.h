#ifndef VFIO_USER_H
#define VFIO_USER_H

/*
 * vfio protocol over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "user-protocol.h"

typedef struct {
    int send_fds;
    int recv_fds;
    int *fds;
} VFIOUserFDs;

typedef struct VFIOUserReply {
    QTAILQ_ENTRY(VFIOUserReply) next;
    VFIOUserHdr *msg;
    VFIOUserFDs *fds;
    uint32_t rsize;
    uint32_t id;
    QemuCond cv;
    bool complete;
    bool nowait;
} VFIOUserReply;


enum proxy_state {
    VFIO_PROXY_CONNECTED = 1,
    VFIO_PROXY_RECV_ERROR = 2,
    VFIO_PROXY_CLOSING = 3,
    VFIO_PROXY_CLOSED = 4,
};

typedef struct VFIOProxy {
    QLIST_ENTRY(VFIOProxy) next;
    char *sockname;
    struct QIOChannel *ioc;
    int (*request)(void *opaque, char *buf, VFIOUserFDs *fds);
    void *reqarg;
    int flags;
    QemuCond close_cv;

    /*
     * above only changed when BQL is held
     * below are protected by per-proxy lock
     */
    QemuMutex lock;
    QTAILQ_HEAD(, VFIOUserReply) free;
    QTAILQ_HEAD(, VFIOUserReply) pending;
    VFIOUserReply *last_nowait;
    enum proxy_state state;
    bool close_wait;
} VFIOProxy;

/* VFIOProxy flags */
#define VFIO_PROXY_CLIENT       0x1
#define VFIO_PROXY_SECURE       0x2

VFIOProxy *vfio_user_connect_dev(SocketAddress *addr, Error **errp);
void vfio_user_disconnect(VFIOProxy *proxy);
uint64_t vfio_user_max_xfer(void);
void vfio_user_set_reqhandler(VFIODevice *vbasdev,
                              int (*handler)(void *opaque, char *buf,
                                             VFIOUserFDs *fds),
                                             void *reqarg);
void vfio_user_send_reply(VFIOProxy *proxy, char *buf, int ret);
int vfio_user_validate_version(VFIODevice *vbasedev, Error **errp);
int vfio_user_dma_map(VFIOProxy *proxy, struct vfio_iommu_type1_dma_map *map,
                      VFIOUserFDs *fds, bool will_commit);
int vfio_user_dma_unmap(VFIOProxy *proxy,
                        struct vfio_iommu_type1_dma_unmap *unmap,
                        struct vfio_bitmap *bitmap, bool will_commit);
int vfio_user_get_info(VFIODevice *vbasedev);
int vfio_user_get_region_info(VFIODevice *vbasedev, int index,
                              struct vfio_region_info *info, VFIOUserFDs *fds);
int vfio_user_get_irq_info(VFIODevice *vbasedev, struct vfio_irq_info *info);
int vfio_user_set_irqs(VFIODevice *vbasedev, struct vfio_irq_set *irq);
int vfio_user_region_read(VFIODevice *vbasedev, uint32_t index, uint64_t offset,
                          uint32_t count, void *data);
int vfio_user_region_write(VFIODevice *vbasedev, uint32_t index,
                           uint64_t offset, uint32_t count, void *data);
void vfio_user_reset(VFIODevice *vbasedev);
void vfio_user_drain_reqs(VFIOProxy *proxy);

#endif /* VFIO_USER_H */
