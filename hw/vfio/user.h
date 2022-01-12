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

enum msg_type {
    VFIO_MSG_NONE,
    VFIO_MSG_ASYNC,
    VFIO_MSG_WAIT,
    VFIO_MSG_NOWAIT,
    VFIO_MSG_REQ,
};

typedef struct VFIOUserMsg {
    QTAILQ_ENTRY(VFIOUserMsg) next;
    VFIOUserHdr *hdr;
    VFIOUserFDs *fds;
    uint32_t rsize;
    uint32_t id;
    QemuCond cv;
    bool complete;
    enum msg_type type;
} VFIOUserMsg;


enum proxy_state {
    VFIO_PROXY_CONNECTED = 1,
    VFIO_PROXY_ERROR = 2,
    VFIO_PROXY_CLOSING = 3,
    VFIO_PROXY_CLOSED = 4,
};

typedef QTAILQ_HEAD(VFIOUserMsgQ, VFIOUserMsg) VFIOUserMsgQ;

typedef struct VFIOProxy {
    QLIST_ENTRY(VFIOProxy) next;
    char *sockname;
    struct QIOChannel *ioc;
    void (*request)(void *opaque, VFIOUserMsg *msg);
    void *req_arg;
    int flags;
    QemuCond close_cv;
    AioContext *ctx;
    QEMUBH *req_bh;

    /*
     * above only changed when BQL is held
     * below are protected by per-proxy lock
     */
    QemuMutex lock;
    VFIOUserMsgQ free;
    VFIOUserMsgQ pending;
    VFIOUserMsgQ incoming;
    VFIOUserMsgQ outgoing;
    VFIOUserMsg *last_nowait;
    enum proxy_state state;
} VFIOProxy;

/* VFIOProxy flags */
#define VFIO_PROXY_CLIENT        0x1
#define VFIO_PROXY_SECURE        0x2
#define VFIO_PROXY_FORCE_QUEUED  0x4
#define VFIO_PROXY_NO_POST       0x8

VFIOProxy *vfio_user_connect_dev(SocketAddress *addr, Error **errp);
void vfio_user_disconnect(VFIOProxy *proxy);
uint64_t vfio_user_max_xfer(void);
void vfio_user_set_handler(VFIODevice *vbasedev,
                           void (*handler)(void *opaque, VFIOUserMsg *msg),
                           void *reqarg);
void vfio_user_send_reply(VFIOProxy *proxy, VFIOUserHdr *hdr, int size);
void vfio_user_send_error(VFIOProxy *proxy, VFIOUserHdr *hdr, int error);
void vfio_user_putfds(VFIOUserMsg *msg);
int vfio_user_validate_version(VFIODevice *vbasedev, Error **errp);

extern VFIODevIO vfio_dev_io_sock;
extern VFIOContIO vfio_cont_io_sock;

#endif /* VFIO_USER_H */
