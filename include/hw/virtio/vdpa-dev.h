#ifndef _VHOST_VDPA_DEVICE_H
#define _VHOST_VDPA_DEVICE_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-vdpa.h"
#include "qom/object.h"


#define TYPE_VHOST_VDPA_DEVICE "vhost-vdpa-device"
OBJECT_DECLARE_SIMPLE_TYPE(VhostVdpaDevice, VHOST_VDPA_DEVICE)

struct VhostVdpaDevice {
    VirtIODevice parent_obj;
    char *vdpa_dev;
    int32_t bootindex;
    struct vhost_dev dev;
    struct vhost_vdpa vdpa;
    VirtQueue **virtqs;
    uint8_t *config;
    int config_size;
    uint32_t num_queues;
    uint16_t queue_size;
    bool started;
};

#endif
