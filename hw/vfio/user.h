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
 * Each message has a standard header that describes the command
 * being sent, which is almost always a VFIO ioctl().
 *
 * The header may be followed by command-specfic data, such as the
 * region and offset info for read and write commands.
 */

/* commands */
enum vfio_user_command {
    VFIO_USER_VERSION                   = 1,
    VFIO_USER_DMA_MAP                   = 2,
    VFIO_USER_DMA_UNMAP                 = 3,
    VFIO_USER_DEVICE_GET_INFO           = 4,
    VFIO_USER_DEVICE_GET_REGION_INFO    = 5,
    VFIO_USER_DEVICE_GET_REGION_IO_FDS  = 6,
    VFIO_USER_DEVICE_GET_IRQ_INFO       = 7,
    VFIO_USER_DEVICE_SET_IRQS           = 8,
    VFIO_USER_REGION_READ               = 9,
    VFIO_USER_REGION_WRITE              = 10,
    VFIO_USER_DMA_READ                  = 11,
    VFIO_USER_DMA_WRITE                 = 12,
    VFIO_USER_DEVICE_RESET              = 13,
    VFIO_USER_DIRTY_PAGES               = 14,
    VFIO_USER_MAX,
};

/* flags */
#define VFIO_USER_REQUEST       0x0
#define VFIO_USER_REPLY         0x1
#define VFIO_USER_TYPE          0xF

#define VFIO_USER_NO_REPLY      0x10
#define VFIO_USER_ERROR         0x20

typedef struct vfio_user_hdr {
    uint16_t id;
    uint16_t command;
    uint32_t size;
    uint32_t flags;
    uint32_t error_reply;
} vfio_user_hdr_t;

/*
 * VFIO_USER_VERSION
 */
#define VFIO_USER_MAJOR_VER     0
#define VFIO_USER_MINOR_VER     0

struct vfio_user_version {
    vfio_user_hdr_t hdr;
    uint16_t major;
    uint16_t minor;
    char capabilities[];
};


#define VFIO_USER_CAP           "capabilities"

/* "capabilities" members */
#define VFIO_USER_CAP_MAX_FDS   "max_msg_fds"
#define VFIO_USER_CAP_MAX_XFER  "max_data_xfer_size"

#define VFIO_USER_DEF_MAX_FDS   8
#define VFIO_USER_MAX_MAX_FDS   16

#define VFIO_USER_DEF_MAX_XFER  (1024 * 1024)
#define VFIO_USER_MAX_MAX_XFER  (64 * 1024 * 1024)

typedef struct VFIOUserFDs {
    int send_fds;
    int recv_fds;
    int *fds;
} VFIOUserFDs;

typedef struct VFIOUserReply {
    QTAILQ_ENTRY(VFIOUserReply) next;
    vfio_user_hdr_t *msg;
    VFIOUserFDs *fds;
    int rsize;
    uint32_t id;
    QemuCond cv;
    uint8_t complete;
} VFIOUserReply;

enum proxy_state {
    CONNECTED = 1,
    RECV_ERROR = 2,
    CLOSING = 3,
    CLOSED = 4,
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
     * above only changed when iolock is held
     * below are protected by per-proxy lock
     */
    QemuMutex lock;
    QTAILQ_HEAD(, VFIOUserReply) free;
    QTAILQ_HEAD(, VFIOUserReply) pending;
    enum proxy_state state;
    int close_wait;
} VFIOProxy;

#define VFIO_PROXY_CLIENT       0x1

void vfio_user_recv(void *opaque);
void vfio_user_send_reply(VFIOProxy *proxy, char *buf, int ret);
VFIOProxy *vfio_user_connect_dev(char *sockname, Error **errp);
void vfio_user_disconnect(VFIOProxy *proxy);
int vfio_user_validate_version(VFIODevice *vbasedev, Error **errp);
#endif /* VFIO_USER_H */
