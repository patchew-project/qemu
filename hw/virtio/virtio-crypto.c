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

static int32_t virtio_crypto_flush_dataq(VirtIOCryptoQueue *q);

static void virtio_crypto_process(VirtIOCrypto *vcrypto)
{
}

static inline int virtio_crypto_vq2q(int queue_index)
{
    return queue_index;
}

static VirtIOCryptoQueue *
virtio_crypto_get_subqueue(CryptoClientState *cc)
{
    VirtIOCrypto *vcrypto = qemu_get_crypto_legacy_hw_opaque(cc);

    return &vcrypto->vqs[cc->queue_index];
}

static void
virtio_crypto_cipher_session_helper(VirtIODevice *vdev,
           CryptoSymSessionInfo *info,
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
               uint64_t *session_id,
               VirtQueueElement *elem)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    CryptoSymSessionInfo info;
    int ret;
    CryptoClientState *cc;
    int queue_index;;
    uint32_t op_type;
    hwaddr auth_key_gpa;
    void *auth_key_hva;
    struct virtio_crypto_session_input *input;
    hwaddr len;
    size_t input_offset;
    struct iovec *iov = elem->in_sg;

    memset(&info, 0, sizeof(info));
    op_type = sess_req->op_type;
    info.op_type = op_type;

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
    cc = qemu_get_crypto_subqueue(vcrypto->crypto, queue_index);
    ret = qemu_crypto_create_session(cc, &info, session_id);
    if (ret == 0) {
        DPRINTF("create session_id=%" PRIu64 "\n", *session_id);
        /* Set the result, notify the frontend driver soon */
        input->status = VIRTIO_CRYPTO_OP_OK;
        input->session_id = *session_id;

        g_free(info.cipher_key);
        g_free(info.auth_key);
        return 0;
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
    CryptoClientState *cc = vcrypto->crypto->ccs;
    uint64_t session_id;
    uint32_t status;
    int queue_index = virtio_crypto_vq2q(queue_id);
    struct iovec *iov = elem->in_sg;
    size_t status_offset;
    void *in_status_ptr;

    session_id = close_sess_req->session_id;
    DPRINTF("close session, id=%" PRIu64 "\n", session_id);
    cc = qemu_get_crypto_subqueue(vcrypto->crypto, queue_index);
    ret = qemu_crypto_close_session(cc, session_id);
    if (ret == 0) {
        status = VIRTIO_CRYPTO_OP_OK;
    } else {
        error_report("destroy session failed");
        status = VIRTIO_CRYPTO_OP_ERR;
    }

    /* calculate the offset of status bits */
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
    uint64_t session_id = 0;

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
                             queue_id,
                             &session_id,
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

static CryptoSymOpInfo *
virtio_crypto_cipher_op_helper(VirtIODevice *vdev,
           struct virtio_crypto_cipher_para *para,
           struct virtio_crypto_cipher_output *out,
           uint32_t aad_len,
           uint64_t aad_data_addr)
{
    CryptoSymOpInfo *op_info;
    uint32_t src_len, dst_len;
    uint32_t iv_len;
    size_t max_len, curr_size = 0;
    hwaddr iv_gpa, src_gpa;
    void *iv_hva, *src_hva, *aad_hva;
    hwaddr len;

    iv_len = para->iv_len;
    src_len = para->src_data_len;
    dst_len = para->dst_data_len;

    max_len = iv_len + aad_len + src_len + dst_len;
    op_info = g_malloc0(sizeof(CryptoSymOpInfo) + max_len);
    op_info->iv_len = iv_len;
    op_info->src_len = src_len;
    op_info->dst_len = dst_len;
    op_info->aad_len = aad_len;
    /* handle the initilization vector */
    if (op_info->iv_len > 0) {
        len = op_info->iv_len;
        DPRINTF("iv_len=%" PRIu32 "\n", len);
        op_info->iv = op_info->data + curr_size;

        iv_gpa = out->iv_addr;
        iv_hva = cpu_physical_memory_map(iv_gpa, &len, false);
        memcpy(op_info->iv, iv_hva, len);
        cpu_physical_memory_unmap(iv_hva, len, false, len);
        curr_size += len;
    }

    /* handle additional authentication data if exist */
    if (op_info->aad_len > 0) {
        len = op_info->aad_len;
        DPRINTF("aad_len=%" PRIu32 "\n", len);
        op_info->aad_data = op_info->data + curr_size;

        aad_hva = cpu_physical_memory_map(aad_data_addr, &len, false);
        memcpy(op_info->aad_data, aad_hva, len);
        cpu_physical_memory_unmap(aad_hva, len, false, len);
        curr_size += len;
    }

    /* handle the source data */
    if (op_info->src_len > 0) {
        len = op_info->src_len;
        DPRINTF("src_len=%" PRIu32 "\n", len);
        op_info->src = op_info->data + curr_size;

        src_gpa = out->src_data_addr;
        src_hva = cpu_physical_memory_map(src_gpa, &len, false);
        memcpy(op_info->src, src_hva, len);
        cpu_physical_memory_unmap(src_hva, len, false, len);
        curr_size += len;
    }
    op_info->dst = op_info->data + curr_size;
    DPRINTF("dst_len=%" PRIu32 "\n", op_info->dst_len);

    return op_info;
}

static void
virtio_crypto_sym_input_data_helper(VirtIODevice *vdev,
                void *idata_hva,
                uint32_t status,
                CryptoSymOpInfo *sym_op_info)
{
    struct virtio_crypto_sym_input *idata = idata_hva;
    hwaddr dst_gpa, len;
    void *dst_hva;

    idata->status = status;
    if (status != VIRTIO_CRYPTO_OP_OK) {
        return;
    }

    /* save the cipher result */
    dst_gpa = idata->dst_data_addr;
    /* Note: length of dest_data is equal to length of src_data for cipher */
    len = sym_op_info->src_len;
    dst_hva = cpu_physical_memory_map(dst_gpa, &len, true);
    memcpy(dst_hva, sym_op_info->dst, len);
    cpu_physical_memory_unmap(dst_hva, len, true, len);

    if (sym_op_info->op_type ==
                      VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        hwaddr digest_gpa;
        void *digest_hva;

        /* save the digest result */
        digest_gpa = idata->digest_result_addr;
        len = sym_op_info->dst_len - sym_op_info->src_len;
        digest_hva = cpu_physical_memory_map(digest_gpa, &len, true);
        memcpy(digest_hva, sym_op_info->dst + sym_op_info->src_len, len);
        cpu_physical_memory_unmap(digest_hva, len, true, len);
    }
}

static void virtio_crypto_tx_complete(CryptoClientState *cc,
                                      int ret)
{
    VirtIOCrypto *vcrypto = qemu_get_crypto_legacy_hw_opaque(cc);
    VirtIOCryptoQueue *q = virtio_crypto_get_subqueue(cc);
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    uint32_t flags = q->async_tx.flags;

    if (flags == QEMU_CRYPTO_PACKET_FLAG_SYM) {
        CryptoSymOpInfo *sym_op_info = q->async_tx.op_info;

        if (ret > 0) {
            virtio_crypto_sym_input_data_helper(vdev,
                                    q->async_tx.idata_hva,
                                    VIRTIO_CRYPTO_OP_OK,
                                    sym_op_info);
        } else if (ret == -1 || ret == 0) {
            virtio_crypto_sym_input_data_helper(vdev,
                                    q->async_tx.idata_hva,
                                    VIRTIO_CRYPTO_OP_ERR,
                                    sym_op_info);
        } else if (ret == -VIRTIO_CRYPTO_OP_BADMSG) {
            virtio_crypto_sym_input_data_helper(vdev,
                                    q->async_tx.idata_hva,
                                    VIRTIO_CRYPTO_OP_BADMSG,
                                    sym_op_info);
        } else if (ret == -VIRTIO_CRYPTO_OP_INVSESS) {
            virtio_crypto_sym_input_data_helper(vdev,
                                    q->async_tx.idata_hva,
                                    VIRTIO_CRYPTO_OP_INVSESS,
                                    sym_op_info);
        }
    }

    virtqueue_push(q->dataq, q->async_tx.elem,
                sizeof(struct virtio_crypto_op_data_req));
    virtio_notify(vdev, q->dataq);

    g_free(q->async_tx.elem);
    q->async_tx.elem = NULL;

    virtio_queue_set_notification(q->dataq, 1);
    virtio_crypto_flush_dataq(q);
}

static void
virtio_crypto_handle_sym_req(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_data_req *req,
               CryptoSymOpInfo **sym_op_info,
               void **idata_hva,
               VirtQueueElement *elem)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    uint32_t op_type;
    void *idata;
    size_t idata_offset;
    struct iovec *iov = elem->in_sg;
    CryptoSymOpInfo *op_info;

    op_type = req->op_type;

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        op_info = virtio_crypto_cipher_op_helper(vdev, &req->u.cipher.para,
                                              &req->u.cipher.odata, 0, 0);
        op_info->op_type = op_type;
        /* calculate the offset of input data */
        idata_offset = offsetof(struct virtio_crypto_op_data_req,
                                u.sym_req.u.cipher.idata.input);
        idata = (void *)iov[0].iov_base + idata_offset;

    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        uint32_t aad_len;
        uint64_t aad_data_addr;

        aad_len = req->u.chain.odata.aad_len;
        aad_data_addr = req->u.chain.odata.aad_data_addr;
        /* cipher part */
        op_info = virtio_crypto_cipher_op_helper(vdev, &req->u.cipher.para,
                                              &req->u.cipher.odata, aad_len,
                                              aad_data_addr);
        op_info->op_type = op_type;

        /* calculate the offset of input data */
        idata_offset = offsetof(struct virtio_crypto_op_data_req,
                                u.sym_req.u.chain.idata.input);
        idata = (void *)iov[0].iov_base + idata_offset;
    } else {
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        error_report("unsupported cipher type");
        exit(1);
    }

    *sym_op_info = op_info;
    *idata_hva = idata;
}

static int32_t virtio_crypto_flush_dataq(VirtIOCryptoQueue *q)
{
    VirtIOCrypto *vcrypto = q->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    VirtQueueElement *elem;
    int32_t num_packets = 0;
    int queue_index = virtio_crypto_vq2q(virtio_get_queue_index(q->dataq));
    struct virtio_crypto_op_data_req req;
    size_t s;
    int ret;
    struct iovec *iov;
    unsigned int iov_cnt;
    uint32_t opcode;
    uint64_t session_id;
    CryptoSymOpInfo *sym_op_info = NULL;
    void *idata_hva = NULL;

    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return num_packets;
    }

    if (q->async_tx.elem) {
        virtio_queue_set_notification(q->dataq, 0);
        return num_packets;
    }

    for (;;) {
        elem = virtqueue_pop(q->dataq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }

        if (elem->in_num < 1 ||
            iov_size(elem->in_sg, elem->in_num) < sizeof(req)) {
            error_report("virtio-crypto dataq missing headers");
            exit(1);
        }

        iov_cnt = elem->in_num;
        iov = elem->in_sg;

        s = iov_to_buf(iov, iov_cnt, 0, &req, sizeof(req));
        assert(s == sizeof(req));
        opcode = req.header.opcode;
        session_id = req.header.session_id;

        switch (opcode) {
        case VIRTIO_CRYPTO_CIPHER_ENCRYPT:
        case VIRTIO_CRYPTO_CIPHER_DECRYPT:
            virtio_crypto_handle_sym_req(vcrypto,
                             &req.u.sym_req,
                             &sym_op_info,
                             &idata_hva,
                             elem);
            sym_op_info->session_id = session_id;
            ret = qemu_send_crypto_packet_async(
                 qemu_get_crypto_subqueue(vcrypto->crypto, queue_index),
                                      QEMU_CRYPTO_PACKET_FLAG_SYM,
                                      sym_op_info, virtio_crypto_tx_complete);
            if (ret == 0) {
                virtio_queue_set_notification(q->dataq, 0);
                q->async_tx.elem = elem;
                q->async_tx.flags = QEMU_CRYPTO_PACKET_FLAG_SYM;
                q->async_tx.idata_hva = idata_hva;
                q->async_tx.op_info = sym_op_info;
                return -EBUSY;
            } else if (ret < 0) {
                virtio_crypto_sym_input_data_helper(vdev, idata_hva,
                                VIRTIO_CRYPTO_OP_ERR,
                                sym_op_info);
                goto drop;
            } else { /* ret > 0 */
                virtio_crypto_sym_input_data_helper(vdev, idata_hva,
                                VIRTIO_CRYPTO_OP_OK,
                                sym_op_info);
            }
            break;
        case VIRTIO_CRYPTO_HASH:
        case VIRTIO_CRYPTO_MAC:
        case VIRTIO_CRYPTO_AEAD_ENCRYPT:
        case VIRTIO_CRYPTO_AEAD_DECRYPT:
        default:
            error_report("virtio-crypto unsupported dataq opcode: %u",
                         opcode);
            exit(1);
        }

drop:
        virtqueue_push(q->dataq, elem, sizeof(req));
        virtio_notify(vdev, q->dataq);

        if (++num_packets >= vcrypto->tx_burst) {
            break;
        }
    }
    return num_packets;
}

static void virtio_crypto_handle_dataq_bh(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    int queue_index = virtio_crypto_vq2q(virtio_get_queue_index(vq));
    VirtIOCryptoQueue *q = &vcrypto->vqs[queue_index];

    if (unlikely(q->tx_waiting)) {
        return;
    }
    q->tx_waiting = 1;
    /* This happens when device was stopped but VCPU wasn't. */
    if (!vdev->vm_running) {
        return;
    }
    virtio_queue_set_notification(vq, 0);
    qemu_bh_schedule(q->tx_bh);
}

static void virtio_crypto_dataq_bh(void *opaque)
{
    VirtIOCryptoQueue *q = opaque;
    VirtIOCrypto *vcrypto = q->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    int32_t ret;

    /* This happens when device was stopped but BH wasn't. */
    if (!vdev->vm_running) {
        /* Make sure tx waiting is set, so we'll run when restarted. */
        assert(q->tx_waiting);
        return;
    }

    q->tx_waiting = 0;

    /* Just in case the driver is not ready on more */
    if (unlikely(!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))) {
        return;
    }

    ret = virtio_crypto_flush_dataq(q);
    if (ret == -EBUSY) {
        return; /* Notification re-enable handled by tx_complete */
    }

    /* If we flush a full burst of packets, assume there are
     * more coming and immediately reschedule */
    if (ret >= vcrypto->tx_burst) {
        qemu_bh_schedule(q->tx_bh);
        q->tx_waiting = 1;
        return;
    }

    /* If less than a full burst, re-enable notification and flush
     * anything that may have come in while we weren't looking.  If
     * we find something, assume the guest is still active and reschedule */
    virtio_queue_set_notification(q->dataq, 1);
    if (virtio_crypto_flush_dataq(q) > 0) {
        virtio_queue_set_notification(q->dataq, 0);
        qemu_bh_schedule(q->tx_bh);
        q->tx_waiting = 1;
    }
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

    crypto_cfg.status = c->status;
    crypto_cfg.max_dataqueues = c->max_queues;
    crypto_cfg.crypto_services = c->legacy_conf.crypto_services;
    crypto_cfg.cipher_algo_l = c->legacy_conf.cipher_algo_l;
    crypto_cfg.cipher_algo_h = c->legacy_conf.cipher_algo_h;
    crypto_cfg.hash_algo = c->legacy_conf.hash_algo;
    crypto_cfg.mac_algo_l = c->legacy_conf.mac_algo_l;
    crypto_cfg.mac_algo_h = c->legacy_conf.mac_algo_h;
    crypto_cfg.asym_algo = c->legacy_conf.asym_algo;
    crypto_cfg.kdf_algo = c->legacy_conf.kdf_algo;
    crypto_cfg.aead_algo = c->legacy_conf.aead_algo;
    crypto_cfg.primitive_algo = c->legacy_conf.primitive_algo;

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
