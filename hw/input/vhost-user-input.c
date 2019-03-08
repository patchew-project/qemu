/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"

#include "hw/qdev.h"
#include "hw/virtio/virtio-input.h"

static void vhost_input_realize(DeviceState *dev, Error **errp)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(dev);
    VirtIOInput *vinput = VIRTIO_INPUT(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    virtio_input_config *config;
    int i, ret;

    if (vhost_user_backend_dev_init(vhi->vhost, vdev, 2, errp) == -1) {
        return;
    }

    ret = vhost_user_input_get_config(&vhi->vhost->dev, &config);
    if (ret < 0) {
        error_setg(errp, "failed to get input config");
        return;
    }
    for (i = 0; i < ret; i++) {
        virtio_input_add_config(vinput, &config[i]);
    }
    g_free(config);
}

static void vhost_input_change_active(VirtIOInput *vinput)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(vinput);

    if (vinput->active) {
        vhost_user_backend_start(vhi->vhost);
    } else {
        vhost_user_backend_stop(vhi->vhost);
    }
}

static const VMStateDescription vmstate_vhost_input = {
    .name = "vhost-user-input",
    .unmigratable = 1,
};

static void vhost_input_class_init(ObjectClass *klass, void *data)
{
    VirtIOInputClass *vic = VIRTIO_INPUT_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd           = &vmstate_vhost_input;
    vic->realize       = vhost_input_realize;
    vic->change_active = vhost_input_change_active;
}

static void vhost_input_init(Object *obj)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(obj);
    VirtIOInput *vinput = VIRTIO_INPUT(obj);
    struct virtio_input_config vhost_input_config[] = { { /* empty list */ } };

    virtio_input_init_config(vinput, vhost_input_config);

    vhi->vhost = VHOST_USER_BACKEND(object_new(TYPE_VHOST_USER_BACKEND));
    object_property_add_alias(obj, "chardev",
                              OBJECT(vhi->vhost), "chardev", &error_abort);
}

static void vhost_input_finalize(Object *obj)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(obj);

    object_unref(OBJECT(vhi->vhost));
}

static const TypeInfo vhost_input_info = {
    .name          = TYPE_VHOST_USER_INPUT,
    .parent        = TYPE_VIRTIO_INPUT,
    .instance_size = sizeof(VHostUserInput),
    .instance_init = vhost_input_init,
    .instance_finalize = vhost_input_finalize,
    .class_init    = vhost_input_class_init,
};

/* ----------------------------------------------------------------- */

static void vhost_input_register_types(void)
{
    type_register_static(&vhost_input_info);
}

type_init(vhost_input_register_types)
