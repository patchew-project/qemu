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
#include "standard-headers/linux/virtio_ids.h"

#define VIRTIO_CRYPTO_VM_VERSION 1

/*
 * Transfer virtqueue index to crypto queue index.
 * The control virtqueue is after the data virtqueues
 * so the input value doesn't need to be adjusted
 */
static inline int virtio_crypto_vq2q(int queue_index)
{
    return queue_index;
}

static int
virtio_crypto_cipher_session_helper(VirtIODevice *vdev,
           QCryptoCryptoDevBackendSymSessionInfo *info,
           struct virtio_crypto_cipher_session_para *cipher_para,
           struct iovec *iov, unsigned int *out_num)
{
    unsigned int num = *out_num;

    info->cipher_alg = virtio_ldl_p(vdev, &cipher_para->algo);
    info->key_len = virtio_ldl_p(vdev, &cipher_para->keylen);
    info->direction = virtio_ldl_p(vdev, &cipher_para->op);
    DPRINTF("cipher_alg=%" PRIu32 ", info->direction=%" PRIu32 "\n",
             info->cipher_alg, info->direction);
    /* Get cipher key */
    if (info->key_len > 0) {
        size_t s;
        DPRINTF("keylen=%" PRIu32 "\n", info->key_len);

        info->cipher_key = g_malloc(info->key_len);
        s = iov_to_buf(iov, num, 0, info->cipher_key, info->key_len);
        if (unlikely(s != info->key_len)) {
            virtio_error(vdev, "virtio-crypto cipher key incorrect");
            return -1;
        }
        iov_discard_front(&iov, &num, info->key_len);
        *out_num = num;
    }

    return 0;
}

static int64_t
virtio_crypto_create_sym_session(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_create_session_req *sess_req,
               uint32_t queue_id,
               uint32_t opcode,
               struct iovec *iov, unsigned int out_num)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    QCryptoCryptoDevBackendSymSessionInfo info;
    int64_t session_id;
    int queue_index;
    uint32_t op_type;
    Error *local_err = NULL;
    int ret;

    memset(&info, 0, sizeof(info));
    op_type = virtio_ldl_p(vdev, &sess_req->op_type);
    info.op_type = op_type;
    info.op_code = opcode;

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        ret = virtio_crypto_cipher_session_helper(vdev, &info,
                           &sess_req->u.cipher.para,
                           iov, &out_num);
        if (ret < 0) {
            return -1;
        }
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        size_t s;
        /* cipher part */
        ret = virtio_crypto_cipher_session_helper(vdev, &info,
                           &sess_req->u.chain.para.cipher_param,
                           iov, &out_num);
        if (ret < 0) {
            return -1;
        }
        /* hash part */
        info.alg_chain_order = virtio_ldl_p(vdev,
                                       &sess_req->u.chain.para.alg_chain_order);
        info.add_len = virtio_ldl_p(vdev, &sess_req->u.chain.para.aad_len);
        info.hash_mode = virtio_ldl_p(vdev, &sess_req->u.chain.para.hash_mode);
        if (info.hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_AUTH) {
            info.hash_alg = virtio_ldl_p(vdev,
                               &sess_req->u.chain.para.u.mac_param.algo);
            info.auth_key_len = virtio_ldl_p(vdev,
                             &sess_req->u.chain.para.u.mac_param.auth_key_len);
            info.hash_result_len = virtio_ldl_p(vdev,
                           &sess_req->u.chain.para.u.mac_param.hash_result_len);
            /* get auth key */
            if (info.auth_key_len > 0) {
                DPRINTF("auth_keylen=%" PRIu32 "\n", info.auth_key_len);
                info.auth_key = g_malloc(info.auth_key_len);
                s = iov_to_buf(iov, out_num, 0, info.auth_key,
                               info.auth_key_len);
                if (unlikely(s != info.auth_key_len)) {
                    virtio_error(vdev,
                          "virtio-crypto authenticated key incorrect");
                    goto err;
                }
                iov_discard_front(&iov, &out_num, info.auth_key_len);
            }
        } else if (info.hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_PLAIN) {
            info.hash_alg = virtio_ldl_p(vdev,
                             &sess_req->u.chain.para.u.hash_param.algo);
            info.hash_result_len = virtio_ldl_p(vdev,
                        &sess_req->u.chain.para.u.hash_param.hash_result_len);
        } else {
            /* VIRTIO_CRYPTO_SYM_HASH_MODE_NESTED */
            virtio_error(vdev, "unsupported hash mode");
            goto err;
        }
    } else {
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        virtio_error(vdev, "unsupported cipher type");
        goto err;
    }

    queue_index = virtio_crypto_vq2q(queue_id);
    session_id = qcrypto_cryptodev_backend_sym_create_session(
                                     vcrypto->cryptodev,
                                     &info, queue_index, &local_err);
    if (session_id >= 0) {
        DPRINTF("create session_id=%" PRIu64 " successfully\n",
                session_id);

        g_free(info.cipher_key);
        g_free(info.auth_key);
        return session_id;
    } else {
        if (local_err) {
            error_report_err(local_err);
        }
    }

err:
    g_free(info.cipher_key);
    g_free(info.auth_key);
    return -1;
}

static uint32_t
virtio_crypto_handle_close_session(VirtIOCrypto *vcrypto,
         struct virtio_crypto_destroy_session_req *close_sess_req,
         uint32_t queue_id)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    int ret;
    uint64_t session_id;
    uint32_t status;
    Error *local_err = NULL;

    session_id = virtio_ldq_p(vdev, &close_sess_req->session_id);
    DPRINTF("close session, id=%" PRIu64 "\n", session_id);

    ret = qcrypto_cryptodev_backend_sym_close_session(
              vcrypto->cryptodev, session_id, queue_id, &local_err);
    if (ret == 0) {
        status = VIRTIO_CRYPTO_OK;
    } else {
        if (local_err) {
            error_report_err(local_err);
        } else {
            error_report("destroy session failed");
        }
        status = VIRTIO_CRYPTO_ERR;
    }

    return status;
}

static void virtio_crypto_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_op_ctrl_req ctrl;
    VirtQueueElement *elem;
    size_t in_len;
    struct iovec *in_iov;
    struct iovec *out_iov;
    unsigned in_num;
    unsigned out_num;
    uint32_t queue_id;
    uint32_t opcode;
    struct virtio_crypto_session_input *input;
    int64_t session_id;
    uint32_t status;
    size_t s;

    for (;;) {
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }
        if (elem->out_num < 1 || elem->in_num < 1) {
            virtio_error(vdev, "virtio-crypto ctrl missing headers");
            g_free(elem);
            break;
        }

        out_num = elem->out_num;
        out_iov = elem->out_sg;
        in_num = elem->in_num;
        in_iov = elem->in_sg;
        if (unlikely(iov_to_buf(out_iov, out_num, 0, &ctrl, sizeof(ctrl))
                    != sizeof(ctrl))) {
            virtio_error(vdev, "virtio-crypto request ctrl_hdr too short");
            g_free(elem);
            break;
        }
        iov_discard_front(&out_iov, &out_num, sizeof(ctrl));

        opcode = virtio_ldl_p(vdev, &ctrl.header.opcode);
        queue_id = virtio_ldl_p(vdev, &ctrl.header.queue_id);

        switch (opcode) {
        case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
            in_len = iov_size(in_iov, in_num);
            input = (void *)in_iov[in_num - 1].iov_base
                      + in_iov[in_num - 1].iov_len
                      - sizeof(*input);
            iov_discard_back(in_iov, &in_num, sizeof(*input));

            session_id = virtio_crypto_create_sym_session(vcrypto,
                             &ctrl.u.sym_create_session,
                             queue_id, opcode,
                             out_iov, out_num);
            if (session_id < 0) {
                input->status = VIRTIO_CRYPTO_ERR;
            } else {
                input->session_id = session_id;
                input->status = VIRTIO_CRYPTO_OK;
            }

            virtqueue_push(vq, elem, in_len);
            virtio_notify(vdev, vq);
            break;
        case VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION:
        case VIRTIO_CRYPTO_HASH_DESTROY_SESSION:
        case VIRTIO_CRYPTO_MAC_DESTROY_SESSION:
        case VIRTIO_CRYPTO_AEAD_DESTROY_SESSION:
            status = virtio_crypto_handle_close_session(vcrypto,
                   &ctrl.u.destroy_session, queue_id);

            s = iov_from_buf(in_iov, in_num, 0, &status, sizeof(status));
            if (unlikely(s != sizeof(status))) {
                virtio_error(vdev, "virtio-crypto status incorrect");
                break;
            }
            virtqueue_push(vq, elem, sizeof(status));
            virtio_notify(vdev, vq);
            break;
        case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
        case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
        case VIRTIO_CRYPTO_AEAD_CREATE_SESSION:
            in_len = iov_size(in_iov, in_num);
            input = (void *)in_iov[in_num - 1].iov_base
                      + in_iov[in_num - 1].iov_len
                      - sizeof(*input);
            error_report("virtio-crypto unsupported ctrl opcode: %d", opcode);

            input->status = VIRTIO_CRYPTO_NOTSUPP;
            virtqueue_push(vq, elem, in_len);
            virtio_notify(vdev, vq);

            break;
        default:
            virtio_error(vdev, "virtio-crypto unsupported ctrl opcode: %u",
                         opcode);
            break;
        } /* end switch case */

        g_free(elem);
    } /* end for loop */
}

static void virtio_crypto_init_request(VirtIOCrypto *vcrypto, VirtQueue *vq,
                                VirtIOCryptoReq *req)
{
    req->vcrypto = vcrypto;
    req->vq = vq;
    req->in = NULL;
}

static void virtio_crypto_free_request(VirtIOCryptoReq *req)
{
    if (req) {
        if (req->flags == QCRYPTO_CRYPTODEV_BACKEND_ALG_SYM) {
            g_free(req->u.sym_op_info);
        }
        g_free(req);
    }
}

static void
virtio_crypto_sym_input_data_helper(VirtIODevice *vdev,
                VirtIOCryptoReq *req,
                uint32_t status,
                QCryptoCryptoDevBackendSymOpInfo *sym_op_info)
{
    size_t s, len;

    if (status != VIRTIO_CRYPTO_OK) {
        return;
    }

    len = sym_op_info->dst_len;
    /* Save the cipher result */
    s = iov_from_buf(req->in_iov, req->in_num, 0, sym_op_info->dst, len);
    if (s != len) {
        virtio_error(vdev, "virtio-crypto dest data incorrect");
        return;
    }

    iov_discard_front(&req->in_iov, &req->in_num, len);

    if (sym_op_info->op_type ==
                      VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        /* Save the digest result */
        s = iov_from_buf(req->in_iov, req->in_num, 0,
                         sym_op_info->digest_result,
                         sym_op_info->digest_result_len);
        if (s != len) {
            virtio_error(vdev, "virtio-crypto digest result incorrect");
        }
    }
}

static void virtio_crypto_req_complete(VirtIOCryptoReq *req, uint32_t status)
{
    VirtIOCrypto *vcrypto = req->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);

    if (req->flags == QCRYPTO_CRYPTODEV_BACKEND_ALG_SYM) {
        virtio_crypto_sym_input_data_helper(vdev, req, status,
                                            req->u.sym_op_info);
    }
    stl_he_p(&req->in->status, status);
    virtqueue_push(req->vq, &req->elem, req->in_len);
    virtio_notify(vdev, req->vq);
}

static VirtIOCryptoReq *
virtio_crypto_get_request(VirtIOCrypto *s, VirtQueue *vq)
{
    VirtIOCryptoReq *req = virtqueue_pop(vq, sizeof(VirtIOCryptoReq));

    if (req) {
        virtio_crypto_init_request(s, vq, req);
    }
    return req;
}

static QCryptoCryptoDevBackendSymOpInfo *
virtio_crypto_sym_op_helper(VirtIODevice *vdev,
           struct virtio_crypto_cipher_para *para,
           uint32_t aad_len,
           struct iovec *iov, unsigned int out_num,
           uint32_t hash_result_len,
           uint32_t hash_start_src_offset)
{
    QCryptoCryptoDevBackendSymOpInfo *op_info;
    uint32_t src_len, dst_len;
    uint32_t iv_len;
    size_t max_len, curr_size = 0;
    size_t s;

    iv_len = virtio_ldl_p(vdev, &para->iv_len);
    src_len = virtio_ldl_p(vdev, &para->src_data_len);
    dst_len = virtio_ldl_p(vdev, &para->dst_data_len);

    max_len = iv_len + aad_len + src_len + dst_len + hash_result_len;
    op_info = g_malloc0(sizeof(QCryptoCryptoDevBackendSymOpInfo) + max_len);
    op_info->iv_len = iv_len;
    op_info->src_len = src_len;
    op_info->dst_len = dst_len;
    op_info->aad_len = aad_len;
    op_info->digest_result_len = hash_result_len;
    op_info->hash_start_src_offset = hash_start_src_offset;
    /* Handle the initilization vector */
    if (op_info->iv_len > 0) {
        DPRINTF("iv_len=%" PRIu32 "\n", op_info->iv_len);
        op_info->iv = op_info->data + curr_size;

        s = iov_to_buf(iov, out_num, 0, op_info->iv, op_info->iv_len);
        if (unlikely(s != op_info->iv_len)) {
            virtio_error(vdev, "virtio-crypto iv incorrect");
            goto err;
        }
        iov_discard_front(&iov, &out_num, op_info->iv_len);
        curr_size += op_info->iv_len;
    }

    /* Handle additional authentication data if exist */
    if (op_info->aad_len > 0) {
        DPRINTF("aad_len=%" PRIu32 "\n", op_info->aad_len);
        op_info->aad_data = op_info->data + curr_size;

        s = iov_to_buf(iov, out_num, 0, op_info->aad_data, op_info->aad_len);
        if (unlikely(s != op_info->aad_len)) {
            virtio_error(vdev, "virtio-crypto additional auth data incorrect");
            goto err;
        }
        iov_discard_front(&iov, &out_num, op_info->aad_len);

        curr_size += op_info->aad_len;
    }

    /* Handle the source data */
    if (op_info->src_len > 0) {
        DPRINTF("src_len=%" PRIu32 "\n", op_info->src_len);
        op_info->src = op_info->data + curr_size;

        s = iov_to_buf(iov, out_num, 0, op_info->src, op_info->src_len);
        if (unlikely(s != op_info->src_len)) {
            virtio_error(vdev, "virtio-crypto source data incorrect");
            goto err;
        }
        iov_discard_front(&iov, &out_num, op_info->src_len);

        curr_size += op_info->src_len;
    }

    /* Handle the destination data */
    op_info->dst = op_info->data + curr_size;
    curr_size += op_info->dst_len;

    DPRINTF("dst_len=%" PRIu32 "\n", op_info->dst_len);

    /* Handle the hash digest result */
    if (hash_result_len > 0) {
        DPRINTF("hash_result_len=%" PRIu32 "\n", hash_result_len);
        op_info->digest_result = op_info->data + curr_size;
    }

    return op_info;

err:
    g_free(op_info);
    return NULL;
}

static int
virtio_crypto_handle_sym_req(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_data_req *req,
               QCryptoCryptoDevBackendSymOpInfo **sym_op_info,
               struct iovec *iov, unsigned int out_num)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    uint32_t op_type;
    QCryptoCryptoDevBackendSymOpInfo *op_info;

    op_type = virtio_ldl_p(vdev, &req->op_type);

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        op_info = virtio_crypto_sym_op_helper(vdev, &req->u.cipher.para,
                                              0, iov, out_num, 0, 0);
        if (!op_info) {
            return -1;
        }
        op_info->op_type = op_type;
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        uint32_t aad_len, hash_result_len;
        uint32_t hash_start_src_offset;

        aad_len = virtio_ldl_p(vdev, &req->u.chain.para.aad_len);
        hash_result_len = virtio_ldl_p(vdev,
                              &req->u.chain.para.hash_result_len);
        hash_start_src_offset = virtio_ldl_p(vdev,
                         &req->u.chain.para.hash_start_src_offset);
        /* cipher part */
        op_info = virtio_crypto_sym_op_helper(vdev, &req->u.chain.para.cipher,
                                              aad_len, iov, out_num,
                                              hash_result_len,
                                              hash_start_src_offset);
        if (!op_info) {
            return -1;
        }
        op_info->op_type = op_type;
    } else {
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        error_report("unsupported cipher type");
        return -1;
    }

    *sym_op_info = op_info;

    return 0;
}

static int
virtio_crypto_handle_request(VirtIOCryptoReq *request)
{
    VirtIOCrypto *vcrypto = request->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    VirtQueueElement *elem = &request->elem;
    int queue_index = virtio_crypto_vq2q(virtio_get_queue_index(request->vq));
    struct virtio_crypto_op_data_req req;
    int ret;
    struct iovec *in_iov;
    struct iovec *out_iov;
    unsigned in_num;
    unsigned out_num;
    uint32_t opcode, status = VIRTIO_CRYPTO_ERR;
    uint64_t session_id;
    QCryptoCryptoDevBackendSymOpInfo *sym_op_info = NULL;
    Error *local_err = NULL;

    if (elem->out_num < 1 || elem->in_num < 1) {
        virtio_error(vdev, "virtio-crypto dataq missing headers");
        return -1;
    }

    out_num = elem->out_num;
    out_iov = elem->out_sg;
    in_num = elem->in_num;
    in_iov = elem->in_sg;
    if (unlikely(iov_to_buf(out_iov, out_num, 0, &req, sizeof(req))
                != sizeof(req))) {
        virtio_error(vdev, "virtio-crypto request outhdr too short");
        return -1;
    }
    iov_discard_front(&out_iov, &out_num, sizeof(req));

    if (in_iov[in_num - 1].iov_len <
            sizeof(struct virtio_crypto_inhdr)) {
        virtio_error(vdev, "virtio-crypto request inhdr too short");
        return -1;
    }

    request->in_len = iov_size(in_iov, in_num);
    request->in = (void *)in_iov[in_num - 1].iov_base
              + in_iov[in_num - 1].iov_len
              - sizeof(struct virtio_crypto_inhdr);
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_crypto_inhdr));

    /*
     * The length of operation result, including dest_data
     * and digest_result if exist.
     */
    request->in_num = in_num;
    request->in_iov = in_iov;

    opcode = virtio_ldl_p(vdev, &req.header.opcode);
    session_id = virtio_ldq_p(vdev, &req.header.session_id);

    switch (opcode) {
    case VIRTIO_CRYPTO_CIPHER_ENCRYPT:
    case VIRTIO_CRYPTO_CIPHER_DECRYPT:
        ret = virtio_crypto_handle_sym_req(vcrypto,
                         &req.u.sym_req,
                         &sym_op_info,
                         out_iov, out_num);
        if (ret < 0) {
            return -1;
        }
        sym_op_info->session_id = session_id;

        /* Set request's parameter */
        request->flags = QCRYPTO_CRYPTODEV_BACKEND_ALG_SYM;
        request->u.sym_op_info = sym_op_info;
        ret = qcrypto_cryptodev_backend_crypto_operation(vcrypto->cryptodev,
                                request, queue_index, &local_err);
        if (ret < 0) {
            status = -ret;
            if (local_err) {
                error_report_err(local_err);
            }
        } else { /* ret == VIRTIO_CRYPTO_OK */
            status = ret;
        }
        virtio_crypto_req_complete(request, status);
        virtio_crypto_free_request(request);
        break;
    case VIRTIO_CRYPTO_HASH:
    case VIRTIO_CRYPTO_MAC:
    case VIRTIO_CRYPTO_AEAD_ENCRYPT:
    case VIRTIO_CRYPTO_AEAD_DECRYPT:
    default:
        virtio_error(vdev, "virtio-crypto unsupported dataq opcode: %u",
                     opcode);
        return -1;
    }

    return 0;
}

static void virtio_crypto_handle_dataq(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    VirtIOCryptoReq *req;

    while ((req = virtio_crypto_get_request(vcrypto, vq))) {
        if (virtio_crypto_handle_request(req) < 0) {
            virtio_crypto_free_request(req);
            break;
        }
    }
}

static uint64_t virtio_crypto_get_features(VirtIODevice *vdev,
                                           uint64_t features,
                                           Error **errp)
{
    return features;
}

static void virtio_crypto_save(QEMUFile *f, void *opaque, size_t size)
{
    VirtIODevice *vdev = opaque;

    virtio_save(vdev, f);
}

static int virtio_crypto_load(QEMUFile *f, void *opaque, size_t size)
{
    VirtIOCrypto *vcrypto = opaque;
    int ret;

    ret = virtio_load(VIRTIO_DEVICE(vcrypto), f, VIRTIO_CRYPTO_VM_VERSION);
    if (ret != 0) {
        return ret;
    }

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
    vcrypto->conf.aead_algo = vcrypto->conf.cryptodev->conf.aead_algo;
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
                   vcrypto->max_queues, VIRTIO_QUEUE_MAX);
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
}

static void virtio_crypto_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);

    unregister_savevm(dev, "virtio-crypto", vcrypto);

    virtio_cleanup(vdev);
}

VMSTATE_VIRTIO_DEVICE(crypto, VIRTIO_CRYPTO_VM_VERSION,
                      virtio_crypto_load, virtio_crypto_save);

static Property virtio_crypto_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_crypto_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOCrypto *c = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_config crypto_cfg;

    virtio_stl_p(vdev, &crypto_cfg.status, c->status);
    virtio_stl_p(vdev, &crypto_cfg.max_dataqueues, c->max_queues);
    virtio_stl_p(vdev, &crypto_cfg.crypto_services,
                       c->conf.crypto_services);
    virtio_stl_p(vdev, &crypto_cfg.cipher_algo_l,
                       c->conf.cipher_algo_l);
    virtio_stl_p(vdev, &crypto_cfg.cipher_algo_h,
                       c->conf.cipher_algo_h);
    virtio_stl_p(vdev, &crypto_cfg.hash_algo,
                       c->conf.hash_algo);
    virtio_stl_p(vdev, &crypto_cfg.mac_algo_l,
                       c->conf.mac_algo_l);
    virtio_stl_p(vdev, &crypto_cfg.mac_algo_h,
                       c->conf.mac_algo_h);
    virtio_stl_p(vdev, &crypto_cfg.aead_algo,
                       c->conf.aead_algo);

    memcpy(config, &crypto_cfg, c->config_size);
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
    vdc->get_features = virtio_crypto_get_features;
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
