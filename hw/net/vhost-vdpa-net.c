#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-vdpa-net.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "net/vhost-vdpa.h"

static void vhost_vdpa_net_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);

    memcpy(config, &s->netcfg, sizeof(struct virtio_net_config));
}

static void vhost_vdpa_net_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);
    struct virtio_net_config *netcfg = (struct virtio_net_config *)config;
    int ret;

    ret = vhost_dev_set_config(&s->dev, (uint8_t *)netcfg, 0, sizeof(*netcfg),
                               VHOST_SET_CONFIG_TYPE_MASTER);
    if (ret) {
        error_report("set device config space failed");
        return;
    }
}

static uint64_t vhost_vdpa_net_get_features(VirtIODevice *vdev,
                                            uint64_t features,
                                            Error **errp)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);

    virtio_add_feature(&features, VIRTIO_NET_F_CSUM);
    virtio_add_feature(&features, VIRTIO_NET_F_GUEST_CSUM);
    virtio_add_feature(&features, VIRTIO_NET_F_MAC);
    virtio_add_feature(&features, VIRTIO_NET_F_GSO);
    virtio_add_feature(&features, VIRTIO_NET_F_GUEST_TSO4);
    virtio_add_feature(&features, VIRTIO_NET_F_GUEST_TSO6);
    virtio_add_feature(&features, VIRTIO_NET_F_GUEST_ECN);
    virtio_add_feature(&features, VIRTIO_NET_F_GUEST_UFO);
    virtio_add_feature(&features, VIRTIO_NET_F_GUEST_ANNOUNCE);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_TSO4);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_TSO6);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_ECN);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_UFO);
    virtio_add_feature(&features, VIRTIO_NET_F_MRG_RXBUF);
    virtio_add_feature(&features, VIRTIO_NET_F_STATUS);
    virtio_add_feature(&features, VIRTIO_NET_F_CTRL_VQ);
    virtio_add_feature(&features, VIRTIO_NET_F_CTRL_RX);
    virtio_add_feature(&features, VIRTIO_NET_F_CTRL_VLAN);
    virtio_add_feature(&features, VIRTIO_NET_F_CTRL_RX_EXTRA);
    virtio_add_feature(&features, VIRTIO_NET_F_CTRL_MAC_ADDR);
    virtio_add_feature(&features, VIRTIO_NET_F_MQ);

    return vhost_get_features(&s->dev, vdpa_feature_bits, features);
}

static int vhost_vdpa_net_start(VirtIODevice *vdev, Error **errp)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_setg(errp, "binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&s->dev, vdev);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error enabling host notifiers");
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, true);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error binding guest notifier");
        goto err_host_notifiers;
    }

    s->dev.acked_features = vdev->guest_features;

    ret = vhost_dev_start(&s->dev, vdev);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error starting vhost");
        goto err_guest_notifiers;
    }
    s->started = true;

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);
    return ret;
}

static void vhost_vdpa_net_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);
    Error *local_err = NULL;
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (s->dev.started) {
        return;
    }

    /* Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
     * vhost here instead of waiting for .set_status().
     */
    ret = vhost_vdpa_net_start(vdev, &local_err);
    if (ret < 0) {
        error_reportf_err(local_err, "vhost-vdpa-net: start failed: ");
        return;
    }

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < s->dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static void vhost_vdpa_net_stop(VirtIODevice *vdev)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!s->started) {
        return;
    }
    s->started = false;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&s->dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static void vhost_vdpa_net_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);
    bool should_start = virtio_device_started(vdev, status);
    Error *local_err = NULL;
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (s->started == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_vdpa_net_start(vdev, &local_err);
        if (ret < 0) {
            error_reportf_err(local_err, "vhost-vdpa-net: start failed: ");
        }
    } else {
        vhost_vdpa_net_stop(vdev);
    }
}

static void vhost_vdpa_net_unrealize(VHostVdpaNet *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    int i;

    for (i = 0; i < s->queue_pairs * 2; i++) {
        virtio_delete_queue(s->virtqs[i]);
    }
    /* ctrl vq */
    virtio_delete_queue(s->virtqs[i]);

    g_free(s->virtqs);
    virtio_cleanup(vdev);
}

static void vhost_vdpa_net_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);
    int i, ret;

    s->vdpa.device_fd = qemu_open_old(s->vdpa_dev, O_RDWR);
    if (s->vdpa.device_fd == -1) {
        error_setg(errp, "vhost-vdpa-net: open %s failed: %s",
                   s->vdpa_dev, strerror(errno));
        return;
    }

    virtio_init(vdev, "virtio-net", VIRTIO_ID_NET,
                sizeof(struct virtio_net_config));

    s->dev.nvqs = s->queue_pairs * 2 + 1;
    s->dev.vqs = g_new0(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;
    s->dev.vq_index_end = s->dev.nvqs;
    s->dev.backend_features = 0;
    s->started = false;

    s->virtqs = g_new0(VirtQueue *, s->dev.nvqs);
    for (i = 0; i < s->dev.nvqs; i++) {
        s->virtqs[i] = virtio_add_queue(vdev, s->queue_size,
                                        vhost_vdpa_net_handle_output);
    }

    ret = vhost_dev_init(&s->dev, &s->vdpa, VHOST_BACKEND_TYPE_VDPA, 0, NULL);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-net: vhost initialization failed: %s",
                   strerror(-ret));
        goto init_err;
    }

    ret = vhost_dev_get_config(&s->dev, (uint8_t *)&s->netcfg,
                               sizeof(struct virtio_net_config), NULL);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-net: get network config failed");
        goto config_err;
    }

    return;
config_err:
    vhost_dev_cleanup(&s->dev);
init_err:
    vhost_vdpa_net_unrealize(s);
    close(s->vdpa.device_fd);
}

static void vhost_vdpa_net_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostVdpaNet *s = VHOST_VDPA_NET(vdev);

    virtio_set_status(vdev, 0);
    vhost_dev_cleanup(&s->dev);
    vhost_vdpa_net_unrealize(s);
    close(s->vdpa.device_fd);
}

static const VMStateDescription vmstate_vhost_vdpa_net = {
    .name = "vhost-vdpa-net",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void vhost_vdpa_net_instance_init(Object *obj)
{
    VHostVdpaNet *s = VHOST_VDPA_NET(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  "/ethernet-phy@0,0", DEVICE(obj));
}

static Property vhost_vdpa_net_properties[] = {
    DEFINE_PROP_STRING("vdpa-dev", VHostVdpaNet, vdpa_dev),
    DEFINE_PROP_UINT16("queue-pairs", VHostVdpaNet, queue_pairs,
                       VHOST_VDPA_NET_AUTO_QUEUE_PAIRS),
    DEFINE_PROP_UINT32("queue-size", VHostVdpaNet, queue_size,
                       VHOST_VDPA_NET_QUEUE_DEFAULT_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_vdpa_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_vdpa_net_properties);
    dc->vmsd = &vmstate_vhost_vdpa_net;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = vhost_vdpa_net_device_realize;
    vdc->unrealize = vhost_vdpa_net_device_unrealize;
    vdc->get_config = vhost_vdpa_net_get_config;
    vdc->set_config = vhost_vdpa_net_set_config;
    vdc->get_features = vhost_vdpa_net_get_features;
    vdc->set_status = vhost_vdpa_net_set_status;
}

static const TypeInfo vhost_vdpa_net_info = {
    .name = TYPE_VHOST_VDPA_NET,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostVdpaNet),
    .instance_init = vhost_vdpa_net_instance_init,
    .class_init = vhost_vdpa_net_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_vdpa_net_info);
}

type_init(virtio_register_types)
