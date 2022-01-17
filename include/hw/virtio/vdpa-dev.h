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
    int vdpa_dev_fd;
    int32_t bootindex;
};

uint32_t vhost_vdpa_device_get_u32(int fd, unsigned long int cmd, Error **errp);

#endif
