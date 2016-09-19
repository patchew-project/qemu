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

static void virtio_crypto_init_request(VirtIOCrypto *vcrypto, VirtQueue *vq,
                                VirtIOCryptoReq *req)
{
    req->vcrypto = vcrypto;
    req->vq = vq;
    req->idata_hva = NULL;
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
                void *idata_hva,
                uint32_t status,
                QCryptoCryptoDevBackendSymOpInfo *sym_op_info);

static void virtio_crypto_req_complete(VirtIOCryptoReq *req, uint32_t status)
{
    VirtIOCrypto *vcrypto = req->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);

    if (req->flags == QCRYPTO_CRYPTODEV_BACKEND_ALG_SYM) {
        virtio_crypto_sym_input_data_helper(vdev, req->idata_hva, status,
                                            req->u.sym_op_info);
    }

    virtqueue_push(req->vq, &req->elem,
                   sizeof(struct virtio_crypto_op_data_req));
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

static void virtio_crypto_map_iovec(unsigned int *p_num_sg, hwaddr *addr,
                               struct iovec *iov,
                               unsigned int max_num_sg,
                               hwaddr pa, size_t sz,
                               bool is_write)
{
    unsigned num_sg = *p_num_sg;
    assert(num_sg <= max_num_sg);

    if (!sz) {
        error_report("virtio-crypto: zero sized buffers are not allowed");
        exit(1);
    }

    while (sz) {
        hwaddr len = sz;

        if (num_sg == max_num_sg) {
            error_report("virtio-crypto: too many entries "
                        "in the scatter gather list");
            exit(1);
        }

        iov[num_sg].iov_base = cpu_physical_memory_map(pa, &len, is_write);
        iov[num_sg].iov_len = len;
        addr[num_sg] = pa;

        sz -= len;
        pa += len;
        num_sg++;
    }
    *p_num_sg = num_sg;
}

static void virtio_crypto_unmap_iovec(VirtIOCryptoBuffer *buf,
                               unsigned int len,
                               bool is_write)
{
    unsigned int offset;
    int i;

    if (is_write) {
        offset = 0;
        for (i = 0; i < buf->num; i++) {
            size_t size = MIN(len - offset, buf->sg[i].iov_len);

            cpu_physical_memory_unmap(buf->sg[i].iov_base,
                                      buf->sg[i].iov_len,
                                      1, size);

            offset += size;
        }
    } else {
        for (i = 0; i < buf->num; i++) {
            cpu_physical_memory_unmap(buf->sg[i].iov_base,
                                      buf->sg[i].iov_len,
                                      0, buf->sg[i].iov_len);
        }
    }
}

static void *virtio_crypto_read_next_iovec(VirtIODevice *vdev,
                                struct virtio_crypto_iovec *iovec,
                                bool is_write,
                                struct iovec *iov,
                                unsigned int *num)
{
    struct virtio_crypto_iovec *iovec_hva;
    hwaddr pa;
    hwaddr len;

    /* If this descriptor says it doesn't chain, we're done. */
    if (!(iovec->flags & VIRTIO_CRYPTO_IOVEC_F_NEXT)) {
        return NULL;
    }

    pa = iovec->next_iovec;
    len = sizeof(*iovec_hva);
    iovec_hva = cpu_physical_memory_map(pa, &len, is_write);
    assert(len == sizeof(*iovec_hva));

    iov[*num].iov_base = iovec_hva;
    iov[*num].iov_len = len;
    (*num)++;

    return iovec_hva;
}

static void *virtio_crypto_alloc_buf(unsigned num)
{
    VirtIOCryptoBuffer *buf;
    size_t addr_ofs = QEMU_ALIGN_UP(sizeof(*buf), __alignof__(buf->addr[0]));
    size_t addr_end = addr_ofs + num * sizeof(buf->addr[0]);
    size_t sg_ofs = QEMU_ALIGN_UP(addr_end, __alignof__(buf->sg[0]));
    size_t sg_end = sg_ofs + num * sizeof(buf->sg[0]);

    buf = g_malloc(sg_end);
    buf->num = num;

    buf->addr = (void *)buf + addr_ofs;
    buf->sg = (void *)buf + sg_ofs;
    return buf;
}

static void *virtio_crypto_iovec_read(VirtIODevice *vdev,
                      struct virtio_crypto_iovec *iovec,
                      bool is_write)
{

    VirtIOCryptoBuffer *buf;
    hwaddr addr[VIRTIO_CRYPTO_SG_MAX];
    struct iovec iov[VIRTIO_CRYPTO_SG_MAX];
    unsigned int num = 0;
    /* Save virtio_crypto_iov structure's hva information in sg_list */
    struct iovec vc_iov[VIRTIO_CRYPTO_SG_MAX];
    unsigned int vc_num = 0;
    unsigned int i;

    struct virtio_crypto_iovec *p_iovec = iovec;

    /* Collect all the sgs */
    do {
        virtio_crypto_map_iovec(&num, addr, iov,
                           VIRTIO_CRYPTO_SG_MAX,
                           p_iovec->addr, p_iovec->len,
                           is_write);
    } while ((p_iovec = virtio_crypto_read_next_iovec(vdev,
                p_iovec, false, vc_iov, &vc_num))
                != NULL);

    /* Now copy what we have collected and mapped */
    buf = virtio_crypto_alloc_buf(num);
    for (i = 0; i < num; i++) {
        buf->addr[i] = addr[i];
        buf->sg[i] = iov[i];
    }
    /* Unmap all virtio_crypto_iov structure if exists */
    for (i = 0; i < vc_num; i++) {
        cpu_physical_memory_unmap(vc_iov[i].iov_base,
                                  vc_iov[i].iov_len,
                                  false, vc_iov[i].iov_len);
    }

    return buf;
}

static QCryptoCryptoDevBackendSymOpInfo *
virtio_crypto_cipher_op_helper(VirtIODevice *vdev,
           struct virtio_crypto_cipher_para *para,
           struct virtio_crypto_cipher_output *out,
           struct virtio_crypto_iovec *add_data)
{
    QCryptoCryptoDevBackendSymOpInfo *op_info;
    uint32_t src_len, dst_len;
    uint32_t iv_len;
    size_t max_len, curr_size = 0;
    hwaddr iv_gpa;
    void *iv_hva;
    hwaddr len;
    uint32_t aad_len = 0;
    VirtIOCryptoBuffer *buf;
    size_t s;

    iv_len = para->iv_len;
    src_len = para->src_data_len;
    dst_len = para->dst_data_len;

    if (add_data) {
        aad_len = add_data->len;
    }

    max_len = iv_len + aad_len + src_len + dst_len;
    op_info = g_malloc0(sizeof(QCryptoCryptoDevBackendSymOpInfo) + max_len);
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
        DPRINTF("aad_len=%" PRIu32 "\n", len);
        op_info->aad_data = op_info->data + curr_size;

        buf = virtio_crypto_iovec_read(vdev, add_data, false);
        s = iov_to_buf(buf->sg, buf->num, 0, op_info->aad_data,
                       op_info->aad_len);
        assert(s == op_info->aad_len);

        virtio_crypto_unmap_iovec(buf, op_info->aad_len, false);
        g_free(buf);
        curr_size += op_info->aad_len;
    }

    /* handle the source data */
    if (op_info->src_len > 0) {
        DPRINTF("src_len=%" PRIu32 "\n", op_info->src_len);
        op_info->src = op_info->data + curr_size;

        buf = virtio_crypto_iovec_read(vdev, &out->src_data, false);
        s = iov_to_buf(buf->sg, buf->num, 0, op_info->src, op_info->src_len);
        assert(s == op_info->src_len);

        virtio_crypto_unmap_iovec(buf, op_info->src_len, false);
        g_free(buf);

        curr_size += op_info->src_len;
    }
    op_info->dst = op_info->data + curr_size;
    DPRINTF("dst_len=%" PRIu32 "\n", op_info->dst_len);

    return op_info;
}

static void
virtio_crypto_sym_input_data_helper(VirtIODevice *vdev,
                void *idata_hva,
                uint32_t status,
                QCryptoCryptoDevBackendSymOpInfo *sym_op_info)
{
    struct virtio_crypto_sym_input *idata = idata_hva;
    hwaddr len;
    VirtIOCryptoBuffer *buf;
    size_t s;

    idata->status = status;
    if (status != VIRTIO_CRYPTO_OP_OK) {
        return;
    }

    buf = virtio_crypto_iovec_read(vdev, &idata->dst_data, true);
    /* Note: length of dest_data is equal to length of src_data for cipher */
    len = sym_op_info->src_len;
    /* save the cipher result */
    s = iov_from_buf(buf->sg, buf->num, 0, sym_op_info->dst, len);
    assert(s == len);

    virtio_crypto_unmap_iovec(buf, len, false);
    g_free(buf);

    if (sym_op_info->op_type ==
                      VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        hwaddr digest_gpa;
        void *digest_hva;

        /* save the digest result */
        digest_gpa = idata->digest_result_addr;
        len = idata->digest_result_len;
        if (len != sym_op_info->dst_len - sym_op_info->src_len) {
            len = sym_op_info->dst_len - sym_op_info->src_len;
        }
        digest_hva = cpu_physical_memory_map(digest_gpa, &len, true);
        /* find the digest result, then copy it into guest's memory */
        memcpy(digest_hva, sym_op_info->dst + sym_op_info->src_len, len);
        cpu_physical_memory_unmap(digest_hva, len, true, len);
    }
}

static void
virtio_crypto_handle_sym_req(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_data_req *req,
               QCryptoCryptoDevBackendSymOpInfo **sym_op_info,
               void **idata_hva,
               VirtQueueElement *elem)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    uint32_t op_type;
    void *idata;
    size_t idata_offset;
    struct iovec *iov = elem->in_sg;
    QCryptoCryptoDevBackendSymOpInfo *op_info;

    op_type = req->op_type;

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        op_info = virtio_crypto_cipher_op_helper(vdev, &req->u.cipher.para,
                                              &req->u.cipher.odata, NULL);
        op_info->op_type = op_type;
        /* calculate the offset of input data */
        idata_offset = offsetof(struct virtio_crypto_op_data_req,
                                u.sym_req.u.cipher.idata.input);
        idata = (void *)iov[0].iov_base + idata_offset;
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        /* cipher part */
        op_info = virtio_crypto_cipher_op_helper(vdev, &req->u.cipher.para,
                                              &req->u.cipher.odata,
                                              &req->u.chain.odata.add_data);
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

static void
virtio_crypto_handle_request(VirtIOCryptoReq *request)
{
    VirtIOCrypto *vcrypto = request->vcrypto;
    VirtQueueElement *elem = &request->elem;
    int queue_index = virtio_crypto_vq2q(virtio_get_queue_index(request->vq));
    struct virtio_crypto_op_data_req req;
    size_t s;
    int ret;
    struct iovec *iov;
    unsigned int iov_cnt;
    uint32_t opcode, status = VIRTIO_CRYPTO_OP_ERR;
    uint64_t session_id;
    QCryptoCryptoDevBackendSymOpInfo *sym_op_info = NULL;
    void *idata_hva = NULL;
    Error *local_err = NULL;

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

        /* Set request's parameter */
        request->flags = QCRYPTO_CRYPTODEV_BACKEND_ALG_SYM;
        request->u.sym_op_info = sym_op_info;
        request->idata_hva = idata_hva;
        ret = qcrypto_cryptodev_backend_crypto_operation(vcrypto->cryptodev,
                                request, queue_index, &local_err);
        if (ret < 0) {
            status = -ret;
            if (local_err) {
                error_report_err(local_err);
            }
        } else { /* ret == VIRTIO_CRYPTO_OP_OK */
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
        error_report("virtio-crypto unsupported dataq opcode: %u",
                     opcode);
        exit(1);
    }
}

static void virtio_crypto_handle_dataq(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    VirtIOCryptoReq *req;

    while ((req = virtio_crypto_get_request(vcrypto, vq))) {
        virtio_crypto_handle_request(req);
    }
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
