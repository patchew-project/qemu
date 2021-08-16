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
void vfio_user_set_reqhandler(VFIODevice *vbasdev,
                              int (*handler)(void *opaque, char *buf,
                                             VFIOUserFDs *fds),
                                             void *reqarg);
void vfio_user_send_reply(VFIOProxy *proxy, char *buf, int ret);

#endif /* VFIO_USER_H */
