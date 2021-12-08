#ifndef VHOST_VDPA_NET_H
#define VHOST_VDPA_NET_H

#include "standard-headers/linux/virtio_blk.h"
#include "hw/block/block.h"
#include "chardev/char-fe.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-vdpa.h"
#include "hw/virtio/virtio-net.h"
#include "qom/object.h"

#define TYPE_VHOST_VDPA_NET "vhost-vdpa-net"
OBJECT_DECLARE_SIMPLE_TYPE(VHostVdpaNet, VHOST_VDPA_NET)

struct VHostVdpaNet {
    VirtIODevice parent_obj;
    int32_t bootindex;
    struct virtio_net_config netcfg;
    uint16_t queue_pairs;
    uint32_t queue_size;
    struct vhost_dev dev;
    VirtQueue **virtqs;
    struct vhost_vdpa vdpa;
    char *vdpa_dev;
    bool started;
};

#define VHOST_VDPA_NET_AUTO_QUEUE_PAIRS     UINT16_MAX
#define VHOST_VDPA_NET_QUEUE_DEFAULT_SIZE   256

#endif
