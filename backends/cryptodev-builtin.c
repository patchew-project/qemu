/*
 * QEMU Cryptodev backend for QEMU cipher APIs
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/cryptodev.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_crypto.h"
#include "crypto/cipher.h"


/**
 * @TYPE_QCRYPTO_CRYPTODEV_BACKEND_BUILTIN:
 * name of backend that uses QEMU cipher API
 */
#define TYPE_QCRYPTO_CRYPTODEV_BACKEND_BUILTIN "cryptodev-backend-builtin"

#define QCRYPTO_CRYPTODEV_BACKEND_BUILTIN(obj) \
    OBJECT_CHECK(QCryptoCryptoDevBackendBuiltin, \
                 (obj), TYPE_QCRYPTO_CRYPTODEV_BACKEND_BUILTIN)

typedef struct QCryptoCryptoDevBackendBuiltin
                         QCryptoCryptoDevBackendBuiltin;

typedef struct QCryptoCryptoDevBackendBuiltinSession {
    QCryptoCipher *cipher;
    uint8_t direction; /* encryption or decryption */
    uint8_t type; /* cipher? hash? aead? */
    QTAILQ_ENTRY(QCryptoCryptoDevBackendBuiltinSession) next;
} QCryptoCryptoDevBackendBuiltinSession;

/* Max number of symetrical sessions */
#define MAX_NUM_SESSIONS 256


struct QCryptoCryptoDevBackendBuiltin {
    QCryptoCryptoDevBackend parent_obj;

    QCryptoCryptoDevBackendBuiltinSession *sessions[MAX_NUM_SESSIONS];
};

static void qcrypto_cryptodev_backend_builtin_init(
             QCryptoCryptoDevBackend *backend, Error **errp)
{
    /* Only support one queue */
    int queues = MAX(backend->conf.peers.queues, 1);
    size_t i;
    QCryptoCryptoDevBackendClientState *cc;

    for (i = 0; i < queues; i++) {
        cc = qcrypto_cryptodev_backend_new_client(
                  "cryptodev-builtin", NULL);
        snprintf(cc->info_str, sizeof(cc->info_str),
                 "cryptodev-builtin%lu", i);
        cc->queue_index = i;

        backend->conf.peers.ccs[i] = cc;
    }

    backend->conf.crypto_services =
                         1u << VIRTIO_CRYPTO_SERVICE_CIPHER |
                         1u << VIRTIO_CRYPTO_SERVICE_HASH |
                         1u << VIRTIO_CRYPTO_SERVICE_MAC;
    backend->conf.cipher_algo_l = 1u << VIRTIO_CRYPTO_CIPHER_AES_CBC;
    backend->conf.hash_algo = 1u << VIRTIO_CRYPTO_HASH_SHA1;
}

static int
qcrypto_cryptodev_backend_builtin_get_unused_session_index(
      QCryptoCryptoDevBackendBuiltin *builtin)
{
    size_t i;

    for (i = 0; i < MAX_NUM_SESSIONS; i++) {
        if (builtin->sessions[i] == NULL) {
            return i;
        }
    }

    return -1;
}

static int
qcrypto_cryptodev_backend_builtin_get_algo(uint32_t key_len,
                                           Error **errp)
{
    int algo;

    if (key_len == 128 / 8) {
        algo = QCRYPTO_CIPHER_ALG_AES_128;
    } else if (key_len == 192 / 8) {
        algo = QCRYPTO_CIPHER_ALG_AES_192;
    } else if (key_len == 256 / 8) {
        algo = QCRYPTO_CIPHER_ALG_AES_256;
    } else {
        error_setg(errp, "unsupported key length :%u", key_len);
        return -1;
    }

    return algo;
}

static int qcrypto_cryptodev_backend_builtin_create_cipher_session(
                    QCryptoCryptoDevBackendBuiltin *builtin,
                    QCryptoCryptoDevBackendSymSessionInfo *sess_info,
                    Error **errp)
{
    int algo;
    int mode;
    QCryptoCipher *cipher;
    int index;
    QCryptoCryptoDevBackendBuiltinSession *sess;

    if (sess_info->op_type != VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        error_setg(errp, "unsupported optype :%u", sess_info->op_type);
        return -1;
    }

    index = qcrypto_cryptodev_backend_builtin_get_unused_session_index(builtin);
    if (index < 0) {
        error_setg(errp, "the total number of created session exceed %u",
                  MAX_NUM_SESSIONS);
        return -1;
    }

    switch (sess_info->cipher_alg) {
    case VIRTIO_CRYPTO_CIPHER_AES_ECB:
        algo = qcrypto_cryptodev_backend_builtin_get_algo(sess_info->key_len,
                                                          errp);
        if (algo < 0)  {
            return -1;
        }
        mode = QCRYPTO_CIPHER_MODE_ECB;
        break;
    case VIRTIO_CRYPTO_CIPHER_AES_CBC:
        algo = qcrypto_cryptodev_backend_builtin_get_algo(sess_info->key_len,
                                                          errp);
        if (algo < 0)  {
            return -1;
        }
        mode = QCRYPTO_CIPHER_MODE_CBC;
        break;
    case VIRTIO_CRYPTO_CIPHER_AES_CTR:
        algo = qcrypto_cryptodev_backend_builtin_get_algo(sess_info->key_len,
                                                          errp);
        if (algo < 0)  {
            return -1;
        }
        mode = QCRYPTO_CIPHER_MODE_CTR;
        break;
    default:
        error_setg(errp, "unsupported cipher alg :%u",
                   sess_info->cipher_alg);
        return -1;
    }

    cipher = qcrypto_cipher_new(algo, mode,
                               sess_info->cipher_key,
                               sess_info->key_len,
                               errp);
    if (!cipher) {
        return -1;
    }

    sess = g_new0(QCryptoCryptoDevBackendBuiltinSession, 1);
    sess->cipher = cipher;
    sess->direction = sess_info->direction;
    sess->type = sess_info->op_type;

    builtin->sessions[index] = sess;

    return index;
}

static int64_t qcrypto_cryptodev_backend_builtin_sym_create_session(
           QCryptoCryptoDevBackend *backend,
           QCryptoCryptoDevBackendSymSessionInfo *sess_info,
           uint32_t queue_index, Error **errp)
{
    QCryptoCryptoDevBackendBuiltin *builtin =
                      QCRYPTO_CRYPTODEV_BACKEND_BUILTIN(backend);
    int64_t session_id = -1;
    int ret;

    switch (sess_info->op_code) {
    case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
        ret = qcrypto_cryptodev_backend_builtin_create_cipher_session(
                           builtin, sess_info, errp);
        if (ret < 0) {
            return ret;
        } else {
            session_id = ret;
        }
        break;
    case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
    case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
    default:
        error_setg(errp, "unsupported opcode :%" PRIu32 "",
                   sess_info->op_code);
        return -1;
    }

    return session_id;
}

static int qcrypto_cryptodev_backend_builtin_sym_close_session(
           QCryptoCryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index, Error **errp)
{
    QCryptoCryptoDevBackendBuiltin *builtin =
                      QCRYPTO_CRYPTODEV_BACKEND_BUILTIN(backend);

    if (session_id >= MAX_NUM_SESSIONS ||
              builtin->sessions[session_id] == NULL) {
        error_setg(errp, "Cannot find a valid session id: %" PRIu64 "",
                      session_id);
        return -1;
    }

    qcrypto_cipher_free(builtin->sessions[session_id]->cipher);
    g_free(builtin->sessions[session_id]);
    builtin->sessions[session_id] = NULL;
    return 0;
}

static int qcrypto_cryptodev_backend_builtin_sym_operation(
                 QCryptoCryptoDevBackend *backend,
                 QCryptoCryptoDevBackendSymOpInfo *op_info,
                 uint32_t queue_index, Error **errp)
{
    QCryptoCryptoDevBackendBuiltin *builtin =
                      QCRYPTO_CRYPTODEV_BACKEND_BUILTIN(backend);
    QCryptoCryptoDevBackendBuiltinSession *sess;
    int ret;

    if (op_info->session_id >= MAX_NUM_SESSIONS ||
              builtin->sessions[op_info->session_id] == NULL) {
        error_setg(errp, "Cannot find a valid session id: %" PRIu64 "",
                   op_info->session_id);
        return -VIRTIO_CRYPTO_INVSESS;
    }

    sess = builtin->sessions[op_info->session_id];

    ret = qcrypto_cipher_setiv(sess->cipher, op_info->iv,
                               op_info->iv_len, errp);
    if (ret < 0) {
        return -VIRTIO_CRYPTO_ERR;
    }

    if (sess->direction == VIRTIO_CRYPTO_OP_ENCRYPT) {
        ret = qcrypto_cipher_encrypt(sess->cipher, op_info->src,
                                     op_info->dst, op_info->src_len, errp);
        if (ret < 0) {
            return -VIRTIO_CRYPTO_ERR;
        }
    } else {
        ret = qcrypto_cipher_decrypt(sess->cipher, op_info->src,
                                     op_info->dst, op_info->src_len, errp);
        if (ret < 0) {
            return -VIRTIO_CRYPTO_ERR;
        }
    }
    return VIRTIO_CRYPTO_OK;
}

static void qcrypto_cryptodev_backend_builtin_cleanup(
             QCryptoCryptoDevBackend *backend,
             Error **errp)
{
    QCryptoCryptoDevBackendBuiltin *builtin =
                      QCRYPTO_CRYPTODEV_BACKEND_BUILTIN(backend);
    size_t i;
    int queues = backend->conf.peers.queues;
    QCryptoCryptoDevBackendClientState *cc;

    for (i = 0; i < MAX_NUM_SESSIONS; i++) {
        if (builtin->sessions[i] != NULL) {
            qcrypto_cryptodev_backend_builtin_sym_close_session(
                    backend, i, 0, errp);
        }
    }

    for (i = 0; i < queues; i++) {
        cc = backend->conf.peers.ccs[i];
        if (cc) {
            qcrypto_cryptodev_backend_free_client(cc);
            backend->conf.peers.ccs[i] = NULL;
        }
    }
}

static void qcrypto_cryptodev_backend_builtin_finalize(Object *obj)
{

}

static void
qcrypto_cryptodev_backend_builtin_class_init(ObjectClass *oc, void *data)
{
    QCryptoCryptoDevBackendClass *bc = QCRYPTO_CRYPTODEV_BACKEND_CLASS(oc);

    bc->init = qcrypto_cryptodev_backend_builtin_init;
    bc->cleanup = qcrypto_cryptodev_backend_builtin_cleanup;
    bc->create_session = qcrypto_cryptodev_backend_builtin_sym_create_session;
    bc->close_session = qcrypto_cryptodev_backend_builtin_sym_close_session;
    bc->do_sym_op = qcrypto_cryptodev_backend_builtin_sym_operation;
}

static const TypeInfo qcrypto_cryptodev_backend_builtin_info = {
    .name = TYPE_QCRYPTO_CRYPTODEV_BACKEND_BUILTIN,
    .parent = TYPE_QCRYPTO_CRYPTODEV_BACKEND,
    .class_init = qcrypto_cryptodev_backend_builtin_class_init,
    .instance_finalize = qcrypto_cryptodev_backend_builtin_finalize,
    .instance_size = sizeof(QCryptoCryptoDevBackendBuiltin),
};

static void
qcrypto_cryptodev_backend_builtin_register_types(void)
{
    type_register_static(&qcrypto_cryptodev_backend_builtin_info);
}

type_init(qcrypto_cryptodev_backend_builtin_register_types);
