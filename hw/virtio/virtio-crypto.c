/*
 * Virtio crypto Support
 *
 * Based on virtio-net.c
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

static void virtio_crypto_handle_dataq_bh(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void virtio_crypto_dataq_bh(void *opaque)
{
}

static void virtio_crypto_add_queue(VirtIOCrypto *vcrypto, int index)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);

    vcrypto->vqs[index].dataq = virtio_add_queue(vdev, 1024,
                                         virtio_crypto_handle_dataq_bh);
    vcrypto->vqs[index].tx_bh = qemu_bh_new(virtio_crypto_dataq_bh,
                                              &vcrypto->vqs[index]);

    vcrypto->vqs[index].tx_waiting = 0;
    vcrypto->vqs[index].vcrypto = vcrypto;
}

static void virtio_crypto_del_queue(VirtIOCrypto *vcrypto, int index)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    VirtIOCryptoQueue *q = &vcrypto->vqs[index];

    virtio_del_queue(vdev, index);
    qemu_bh_delete(q->tx_bh);
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

static void virtio_crypto_set_status(VirtIODevice *vdev, uint8_t status)
{
}

static void virtio_crypto_reset(VirtIODevice *vdev)
{
    /*
     * This should cancel pending requests, but can't do nicely until there
     * are per-device request lists.
     */
}

static void virtio_crypto_set_hw_status(CryptoClientState *cc)
{
    VirtIOCrypto *vcrypto = qemu_get_crypto_legacy_hw_opaque(cc);
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    uint16_t old_status = vcrypto->status;

    if (!cc->ready) {
        vcrypto->status &= ~VIRTIO_CRYPTO_S_HW_READY;
    } else {
        vcrypto->status |= VIRTIO_CRYPTO_S_HW_READY;
    }
    if (vcrypto->status != old_status) {
        virtio_notify_config(vdev);
    }

    virtio_crypto_set_status(vdev, vdev->status);
}

static CryptoClientInfo crypto_virtio_info = {
    .type = CRYPTO_CLIENT_OPTIONS_KIND_LEGACY_HW,
    .size = sizeof(CryptoLegacyHWState),
    .hw_status_changed = virtio_crypto_set_hw_status,
};

static void virtio_crypto_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    int i;

    vcrypto->max_queues = MAX(vcrypto->legacy_conf.peers.queues, 1);
    if (vcrypto->max_queues + 1 > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "Invalid number of queues (= %" PRIu16 "), "
                   "must be a postive integer less than %d.",
                   vcrypto->max_queues, VIRTIO_QUEUE_MAX - 1);
        return;
    }

    virtio_init(vdev, "virtio-crypto", VIRTIO_ID_CRYPTO, vcrypto->config_size);
    vcrypto->vqs = g_new0(VirtIOCryptoQueue, vcrypto->max_queues);
    vcrypto->curr_queues = 1;

    for (i = 0; i < vcrypto->max_queues; i++) {
        virtio_crypto_add_queue(vcrypto, i);
    }

    vcrypto->ctrl_vq = virtio_add_queue(vdev, 64, virtio_crypto_handle_ctrl);
    vcrypto->tx_burst = vcrypto->conf.txburst;
    vcrypto->crypto = qemu_new_crypto_legacy_hw(&crypto_virtio_info,
                                             &vcrypto->legacy_conf,
                                             object_get_typename(OBJECT(dev)),
                                             dev->id, vcrypto);
    vcrypto->status = VIRTIO_CRYPTO_S_HW_READY;
    register_savevm(dev, "virtio-crypto", -1, 1, virtio_crypto_save,
                    virtio_crypto_load, vcrypto);
}

static void virtio_crypto_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    int i, max_queues;

    unregister_savevm(dev, "virtio-crypto", vcrypto);

    max_queues = vcrypto->multiqueue ? vcrypto->max_queues : 1;
    for (i = 0; i < max_queues; i++) {
        virtio_crypto_del_queue(vcrypto, i);
    }
    g_free(vcrypto->vqs);
    qemu_del_crypto_legacy_hw(vcrypto->crypto);

    virtio_cleanup(vdev);
}

static Property virtio_crypto_properties[] = {
    DEFINE_PROP_CRYPTODEV("cryptodev", VirtIOCrypto, legacy_conf.peers),
    DEFINE_PROP_INT32("x-txburst", VirtIOCrypto, conf.txburst,
                      VIRTIO_CRYPTO_TX_BURST),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_crypto_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOCrypto *c = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_config crypto_cfg;

    virtio_stl_p(vdev, &crypto_cfg.status, c->status);
    virtio_stl_p(vdev, &crypto_cfg.max_dataqueues, c->max_queues);
    virtio_stl_p(vdev, &crypto_cfg.crypto_services,
                 c->legacy_conf.crypto_services);
    virtio_stl_p(vdev, &crypto_cfg.cipher_algo_l,
                 c->legacy_conf.cipher_algo_l);
    virtio_stl_p(vdev, &crypto_cfg.cipher_algo_h,
                 c->legacy_conf.cipher_algo_h);
    virtio_stl_p(vdev, &crypto_cfg.hash_algo,
                 c->legacy_conf.hash_algo);
    virtio_stl_p(vdev, &crypto_cfg.mac_algo_l,
                 c->legacy_conf.mac_algo_l);
    virtio_stl_p(vdev, &crypto_cfg.mac_algo_h,
                 c->legacy_conf.mac_algo_h);
    virtio_stl_p(vdev, &crypto_cfg.asym_algo,
                 c->legacy_conf.asym_algo);
    virtio_stl_p(vdev, &crypto_cfg.kdf_algo,
                 c->legacy_conf.kdf_algo);
    virtio_stl_p(vdev, &crypto_cfg.aead_algo,
                 c->legacy_conf.aead_algo);
    virtio_stl_p(vdev, &crypto_cfg.primitive_algo,
                 c->legacy_conf.primitive_algo);

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
    vdc->set_status = virtio_crypto_set_status;
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
