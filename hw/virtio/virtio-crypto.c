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

static inline int virtio_crypto_vq2q(int queue_index)
{
    return queue_index;
}

static void
virtio_crypto_cipher_session_helper(VirtIODevice *vdev,
           QCryptoCryptoDevBackendSymSessionInfo *info,
           struct virtio_crypto_cipher_session_para *cipher_para,
           struct virtio_crypto_cipher_session_output *cipher_out)
{
    hwaddr key_gpa;
    void *key_hva;
    hwaddr len;

    info->cipher_alg = cipher_para->algo;
    info->key_len = cipher_para->keylen;
    info->direction = cipher_para->op;
    len = info->key_len;
    /* get cipher key */
    if (len > 0) {
        DPRINTF("keylen=%" PRIu32 "\n", info->key_len);
        key_gpa = cipher_out->key_addr;

        key_hva = cpu_physical_memory_map(key_gpa, &len, 0);

        info->cipher_key = g_malloc(info->key_len);
        memcpy(info->cipher_key, key_hva, info->key_len);
        cpu_physical_memory_unmap(key_hva, len, 0, len);
    }
}

static int64_t
virtio_crypto_create_sym_session(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_create_session_req *sess_req,
               uint32_t queue_id,
               uint32_t opcode,
               VirtQueueElement *elem)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    QCryptoCryptoDevBackendSymSessionInfo info;
    int64_t session_id;
    int queue_index;
    uint32_t op_type;
    hwaddr auth_key_gpa;
    void *auth_key_hva;
    struct virtio_crypto_session_input *input;
    hwaddr len;
    size_t input_offset;
    Error *local_err = NULL;
    struct iovec *iov = elem->in_sg;

    memset(&info, 0, sizeof(info));
    op_type = sess_req->op_type;
    info.op_type = op_type;
    info.op_code = opcode;

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        virtio_crypto_cipher_session_helper(vdev, &info,
                           &sess_req->u.cipher.para,
                           &sess_req->u.cipher.out);
        /* calculate the offset of input data */
        input_offset = offsetof(struct virtio_crypto_op_ctrl_req,
                          u.sym_create_session.u.cipher.input);
        input = (void *)iov[0].iov_base + input_offset;
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        /* cipher part */
        virtio_crypto_cipher_session_helper(vdev, &info,
                           &sess_req->u.chain.para.cipher_param,
                           &sess_req->u.chain.out.cipher);
        /* calculate the offset of input data */
        input_offset = offsetof(struct virtio_crypto_op_ctrl_req,
                                u.sym_create_session.u.chain.input);
        input = (void *)iov[0].iov_base + input_offset;
        /* hash part */
        info.alg_chain_order = sess_req->u.chain.para.alg_chain_order;
        info.add_len = sess_req->u.chain.para.aad_len;
        info.hash_mode = sess_req->u.chain.para.hash_mode;
        if (info.hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_AUTH) {
            info.hash_alg = sess_req->u.chain.para.u.mac_param.algo;
            len = info.auth_key_len =
                       sess_req->u.chain.para.u.mac_param.auth_key_len;
            info.hash_result_len =
                    sess_req->u.chain.para.u.mac_param.hash_result_len;
            /* get auth key */
            if (len > 0) {
                DPRINTF("keylen=%" PRIu32 "\n", info.auth_key_len);
                auth_key_gpa = sess_req->u.chain.out.mac.auth_key_addr;
                auth_key_hva = cpu_physical_memory_map(auth_key_gpa,
                               &len, false);
                info.auth_key = g_malloc(len);
                memcpy(info.auth_key, auth_key_hva, len);
                cpu_physical_memory_unmap(auth_key_hva, len, false, len);
            }
        } else if (info.hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_PLAIN) {
            info.hash_alg = sess_req->u.chain.para.u.hash_param.algo;
            info.hash_result_len =
                   sess_req->u.chain.para.u.hash_param.hash_result_len;
        } else {
            /* VIRTIO_CRYPTO_SYM_HASH_MODE_NESTED */
            error_report("unsupported hash mode");
            goto err;
        }
    } else {
        /* calculate the offset of input data */
        input_offset = offsetof(struct virtio_crypto_op_ctrl_req,
                                u.sym_create_session.u.cipher.input);
        input = (void *)iov[0].iov_base + input_offset;
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        error_report("unsupported cipher type");
        goto err;
    }

    queue_index = virtio_crypto_vq2q(queue_id);
    session_id = qcrypto_cryptodev_backend_sym_create_session(
                                     vcrypto->cryptodev,
                                     &info, queue_index, &local_err);
    if (session_id >= 0) {
        DPRINTF("create session_id=%" PRIu64 "\n", session_id);
        /* Set the result, notify the frontend driver soon */
        input->status = VIRTIO_CRYPTO_OP_OK;
        input->session_id = session_id;

        g_free(info.cipher_key);
        g_free(info.auth_key);
        return 0;
    } else {
        if (local_err) {
            error_report_err(local_err);
        }
    }

err:
    g_free(info.cipher_key);
    g_free(info.auth_key);
    input->status = VIRTIO_CRYPTO_OP_ERR;
    return -1;
}

static void
virtio_crypto_handle_close_session(VirtIOCrypto *vcrypto,
         struct virtio_crypto_destroy_session_req *close_sess_req,
         uint32_t queue_id,
         VirtQueueElement *elem)
{
    int ret;
    uint64_t session_id;
    uint32_t status;
    struct iovec *iov = elem->in_sg;
    size_t status_offset;
    void *in_status_ptr;
    Error *local_err = NULL;

    session_id = close_sess_req->session_id;
    DPRINTF("close session, id=%" PRIu64 "\n", session_id);

    ret = qcrypto_cryptodev_backend_sym_close_session(
              vcrypto->cryptodev, session_id, &local_err);
    if (ret == 0) {
        status = VIRTIO_CRYPTO_OP_OK;
    } else {
        if (local_err) {
            error_report_err(local_err);
        } else {
            error_report("destroy session failed");
        }
        status = VIRTIO_CRYPTO_OP_ERR;
    }

    /* Calculate the offset of status bits */
    status_offset = offsetof(struct virtio_crypto_op_ctrl_req,
                             u.destroy_session.status);
    in_status_ptr = (void *)iov[0].iov_base + status_offset;
    /* Set the result, notify the frontend driver soon */
    memcpy(in_status_ptr, &status, sizeof(status));
}

static void virtio_crypto_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_op_ctrl_req ctrl;
    VirtQueueElement *elem;
    size_t s;
    struct iovec *iov;
    unsigned int iov_cnt;
    uint32_t queue_id;
    uint32_t opcode;

    for (;;) {
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }
        if (elem->in_num < 1 ||
            iov_size(elem->in_sg, elem->in_num) < sizeof(ctrl)) {
            error_report("virtio-crypto ctrl missing headers");
            exit(1);
        }

        iov_cnt = elem->in_num;
        iov = elem->in_sg;
        s = iov_to_buf(iov, iov_cnt, 0, &ctrl, sizeof(ctrl));
        assert(s == sizeof(ctrl));
        opcode = ctrl.header.opcode;
        queue_id = ctrl.header.queue_id;

        switch (opcode) {
        case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
            virtio_crypto_create_sym_session(vcrypto,
                             &ctrl.u.sym_create_session,
                             queue_id, opcode,
                             elem);

            break;
        case VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION:
        case VIRTIO_CRYPTO_HASH_DESTROY_SESSION:
        case VIRTIO_CRYPTO_MAC_DESTROY_SESSION:
        case VIRTIO_CRYPTO_AEAD_DESTROY_SESSION:
            virtio_crypto_handle_close_session(vcrypto,
                   &ctrl.u.destroy_session, queue_id,
                   elem);
            break;
        case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
        case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
        case VIRTIO_CRYPTO_AEAD_CREATE_SESSION:
        default:
            error_report("virtio-crypto unsupported ctrl opcode: %u",
                         opcode);
            exit(1);
        }

        virtqueue_push(vq, elem, sizeof(ctrl));
        virtio_notify(vdev, vq);
        g_free(elem);
    }
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
