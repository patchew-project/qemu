/*
 * Virtio crypto Support
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "hw/qdev.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-crypto.h"
#include "hw/virtio/virtio-access.h"

static void virtio_crypto_process(VirtIOCrypto *vcrypto)
{
}

static void virtio_crypto_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{

}

static void virtio_crypto_handle_dataq(VirtIODevice *vdev, VirtQueue *vq)
{

}

static uint64_t virtio_crypto_get_features(VirtIODevice *vdev,
                                           uint64_t features,
                                           Error **errp)
{
    return features;
}

static void virtio_crypto_set_features(VirtIODevice *vdev, uint64_t features)
{

}

static void virtio_crypto_save(QEMUFile *f, void *opaque)
{
    VirtIODevice *vdev = opaque;

    virtio_save(vdev, f);
}

static int virtio_crypto_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOCrypto *vcrypto = opaque;
    int ret;

    if (version_id != 1) {
        return -EINVAL;
    }
    ret = virtio_load(VIRTIO_DEVICE(vcrypto), f, version_id);
    if (ret != 0) {
        return ret;
    }

    /* We may have an element ready but couldn't process it due to a quota
     * limit.  Make sure to try again after live migration when the quota may
     * have been reset.
     */
    virtio_crypto_process(vcrypto);

    return 0;
}

static void virtio_crypto_reset(VirtIODevice *vdev)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    /* multiqueue is disabled by default */
    vcrypto->curr_queues = 1;
    if (!vcrypto->cryptodev->ready) {
        vcrypto->status &= ~VIRTIO_CRYPTO_S_HW_READY;
    } else {
        vcrypto->status |= VIRTIO_CRYPTO_S_HW_READY;
    }
}

static void virtio_crypto_init_config(VirtIODevice *vdev)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);

    vcrypto->conf.crypto_services =
                     vcrypto->conf.cryptodev->conf.crypto_services;
    vcrypto->conf.cipher_algo_l =
                     vcrypto->conf.cryptodev->conf.cipher_algo_l;
    vcrypto->conf.cipher_algo_h =
                     vcrypto->conf.cryptodev->conf.cipher_algo_h;
    vcrypto->conf.hash_algo = vcrypto->conf.cryptodev->conf.hash_algo;
    vcrypto->conf.mac_algo_l = vcrypto->conf.cryptodev->conf.mac_algo_l;
    vcrypto->conf.mac_algo_h = vcrypto->conf.cryptodev->conf.mac_algo_h;
    vcrypto->conf.asym_algo = vcrypto->conf.cryptodev->conf.asym_algo;
    vcrypto->conf.kdf_algo = vcrypto->conf.cryptodev->conf.kdf_algo;
    vcrypto->conf.aead_algo = vcrypto->conf.cryptodev->conf.aead_algo;
    vcrypto->conf.primitive_algo =
                     vcrypto->conf.cryptodev->conf.primitive_algo;
}

static void virtio_crypto_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    int i;

    vcrypto->cryptodev = vcrypto->conf.cryptodev;
    if (vcrypto->cryptodev == NULL) {
        error_setg(errp, "'cryptodev' parameter expects a valid object");
        return;
    }

    vcrypto->max_queues = MAX(vcrypto->cryptodev->conf.peers.queues, 1);
    if (vcrypto->max_queues + 1 > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "Invalid number of queues (= %" PRIu16 "), "
                   "must be a postive integer less than %d.",
                   vcrypto->max_queues, VIRTIO_QUEUE_MAX - 1);
        return;
    }

    virtio_init(vdev, "virtio-crypto", VIRTIO_ID_CRYPTO, vcrypto->config_size);
    vcrypto->curr_queues = 1;

    for (i = 0; i < vcrypto->max_queues; i++) {
        virtio_add_queue(vdev, 1024, virtio_crypto_handle_dataq);
    }

    vcrypto->ctrl_vq = virtio_add_queue(vdev, 64, virtio_crypto_handle_ctrl);
    if (!vcrypto->cryptodev->ready) {
        vcrypto->status &= ~VIRTIO_CRYPTO_S_HW_READY;
    } else {
        vcrypto->status |= VIRTIO_CRYPTO_S_HW_READY;
    }
    virtio_crypto_init_config(vdev);
    register_savevm(dev, "virtio-crypto", -1, 1, virtio_crypto_save,
                    virtio_crypto_load, vcrypto);
}

static void virtio_crypto_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);

    unregister_savevm(dev, "virtio-crypto", vcrypto);

    virtio_cleanup(vdev);
}

static Property virtio_crypto_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_crypto_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOCrypto *c = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_config crypto_cfg;

    crypto_cfg.status = c->status;
    crypto_cfg.max_dataqueues = c->max_queues;
    crypto_cfg.crypto_services = c->conf.crypto_services;
    crypto_cfg.cipher_algo_l = c->conf.cipher_algo_l;
    crypto_cfg.cipher_algo_h = c->conf.cipher_algo_h;
    crypto_cfg.hash_algo = c->conf.hash_algo;
    crypto_cfg.mac_algo_l = c->conf.mac_algo_l;
    crypto_cfg.mac_algo_h = c->conf.mac_algo_h;
    crypto_cfg.asym_algo = c->conf.asym_algo;
    crypto_cfg.kdf_algo = c->conf.kdf_algo;
    crypto_cfg.aead_algo = c->conf.aead_algo;
    crypto_cfg.primitive_algo = c->conf.primitive_algo;

    memcpy(config, &crypto_cfg, c->config_size);
}

static void virtio_crypto_set_config(VirtIODevice *vdev, const uint8_t *config)
{

}

static void virtio_crypto_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_crypto_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_crypto_device_realize;
    vdc->unrealize = virtio_crypto_device_unrealize;
    vdc->get_config = virtio_crypto_get_config;
    vdc->set_config = virtio_crypto_set_config;
    vdc->get_features = virtio_crypto_get_features;
    vdc->set_features = virtio_crypto_set_features;
    vdc->reset = virtio_crypto_reset;
}

static void virtio_crypto_instance_init(Object *obj)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(obj);

    /*
     * The default config_size is sizeof(struct virtio_crypto_config).
     * Can be overriden with virtio_crypto_set_config_size.
     */
    vcrypto->config_size = sizeof(struct virtio_crypto_config);

    object_property_add_link(obj, "cryptodev",
                             TYPE_QCRYPTO_CRYPTODEV_BACKEND,
                             (Object **)&vcrypto->conf.cryptodev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);
}

static const TypeInfo virtio_crypto_info = {
    .name = TYPE_VIRTIO_CRYPTO,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOCrypto),
    .instance_init = virtio_crypto_instance_init,
    .class_init = virtio_crypto_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_crypto_info);
}

type_init(virtio_register_types)
