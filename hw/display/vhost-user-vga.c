/*
 * vhost-user VGA device
 *
 * Copyright Red Hat, Inc. 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "virtio-vga.h"

#define TYPE_VHOST_USER_VGA "vhost-user-vga"

#define VHOST_USER_VGA(obj)                                \
    OBJECT_CHECK(VhostUserVGA, (obj), TYPE_VHOST_USER_VGA)

typedef struct VhostUserVGA {
    VirtIOVGABase parent_obj;

    VhostUserGPU vdev;
} VhostUserVGA;

static void vhost_user_vga_inst_initfn(Object *obj)
{
    VhostUserVGA *dev = VHOST_USER_VGA(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_GPU);

    VIRTIO_VGA_BASE(dev)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);

    object_property_add_alias(obj, "vhost-user",
                              OBJECT(&dev->vdev), "vhost-user",
                              &error_abort);
}

static TypeInfo vhost_user_vga_info = {
    .name          = TYPE_VHOST_USER_VGA,
    .parent        = TYPE_VIRTIO_VGA_BASE,
    .instance_size = sizeof(struct VhostUserVGA),
    .instance_init = vhost_user_vga_inst_initfn,
};

static void vhost_user_vga_register_types(void)
{
    type_register_static(&vhost_user_vga_info);
}

type_init(vhost_user_vga_register_types)
