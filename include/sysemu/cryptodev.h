/*
 * QEMU Crypto Device Implementation
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
#ifndef QCRYPTO_CRYPTODEV_H
#define QCRYPTO_CRYPTODEV_H

#include "qom/object.h"
#include "qemu-common.h"

/**
 * QCryptoCryptoDevBackend:
 *
 * The QCryptoCryptoDevBackend object is an interface
 * for different cryptodev backends, which provides crypto
 * operation wrapper.
 *
 */

#define TYPE_QCRYPTO_CRYPTODEV_BACKEND "cryptodev-backend"

#define QCRYPTO_CRYPTODEV_BACKEND(obj) \
    OBJECT_CHECK(QCryptoCryptoDevBackend, \
                 (obj), TYPE_QCRYPTO_CRYPTODEV_BACKEND)
#define QCRYPTO_CRYPTODEV_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(QCryptoCryptoDevBackendClass, \
                 (obj), TYPE_QCRYPTO_CRYPTODEV_BACKEND)
#define QCRYPTO_CRYPTODEV_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(QCryptoCryptoDevBackendClass, \
                (klass), TYPE_QCRYPTO_CRYPTODEV_BACKEND)


#define MAX_CRYPTO_QUEUE_NUM  64

typedef struct QCryptoCryptoDevBackendConf QCryptoCryptoDevBackendConf;
typedef struct QCryptoCryptoDevBackendPeers QCryptoCryptoDevBackendPeers;
typedef struct QCryptoCryptoDevBackendClientState
                     QCryptoCryptoDevBackendClientState;
typedef struct QCryptoCryptoDevBackend QCryptoCryptoDevBackend;

enum QCryptoCryptoDevBackendAlgType {
    QCRYPTO_CRYPTODEV_BACKEND_ALG_SYM,
    QCRYPTO_CRYPTODEV_BACKEND_ALG__MAX,
};

/**
 * QCryptoCryptoDevBackendSymSessionInfo:
 *
 * @op_code: operation code (refer to virtio_crypto.h)
 * @cipher_alg: algorithm type of CIPHER
 * @key_len: byte length of cipher key
 * @hash_alg: algorithm type of HASH/MAC
 * @hash_result_len: byte length of HASH operation result
 * @auth_key_len: byte length of authenticated key
 * @add_len: byte length of additional authenticated data
 * @op_type: operation type (refer to virtio_crypto.h)
 * @direction: encryption or direction for CIPHER
 * @hash_mode: HASH mode for HASH operation (refer to virtio_crypto.h)
 * @alg_chain_order: order of algorithm chaining (CIPHER then HASH,
 *                   or HASH then CIPHER)
 * @cipher_key: point to a key of CIPHER
 * @auth_key: point to an authenticated key of MAC
 *
 */
typedef struct QCryptoCryptoDevBackendSymSessionInfo {
    /* corresponding with virtio crypto spec */
    uint32_t op_code;
    uint32_t cipher_alg;
    uint32_t key_len;
    uint32_t hash_alg;
    uint32_t hash_result_len;
    uint32_t auth_key_len;
    uint32_t add_len;
    uint8_t op_type;
    uint8_t direction;
    uint8_t hash_mode;
    uint8_t alg_chain_order;
    uint8_t *cipher_key;
    uint8_t *auth_key;
} QCryptoCryptoDevBackendSymSessionInfo;

/**
 * QCryptoCryptoDevBackendSymOpInfo:
 *
 * @session_id: session index which was previously
 *              created by qcrypto_cryptodev_backend_sym_create_session()
 * @aad_len: byte length of additional authenticated data
 * @iv_len: byte length of initialization vector or counter
 * @src_len: byte length of source data
 * @dst_len: byte length of destination data
 * @digest_result_len: byte length of hash digest result
 * @hash_start_src_offset: Starting point for hash processing, specified
 *                 as number of bytes from start of packet in source data
 * @op_type: operation type (refer to virtio_crypto.h)
 * @iv: point to the initialization vector or counter
 * @src: point to the source data
 * @dst: point to the destination data
 * @aad_data: point to the additional authenticated data
 * @digest_result: point to the digest result data
 * @data[0]: point to the extensional memory by one memory allocation
 *
 */
typedef struct QCryptoCryptoDevBackendSymOpInfo {
    uint64_t session_id;
    uint32_t aad_len;
    uint32_t iv_len;
    uint32_t src_len;
    uint32_t dst_len;
    uint32_t digest_result_len;
    uint32_t hash_start_src_offset;
    uint8_t op_type;
    uint8_t *iv;
    uint8_t *src;
    uint8_t *dst;
    uint8_t *aad_data;
    uint8_t *digest_result;
    uint8_t data[0];
} QCryptoCryptoDevBackendSymOpInfo;

typedef struct QCryptoCryptoDevBackendClass {
    ObjectClass parent_class;

    void (*init)(QCryptoCryptoDevBackend *backend, Error **errp);
    void (*cleanup)(QCryptoCryptoDevBackend *backend, Error **errp);

    int64_t (*create_session)(QCryptoCryptoDevBackend *backend,
                       QCryptoCryptoDevBackendSymSessionInfo *sess_info,
                       uint32_t queue_index, Error **errp);
    int (*close_session)(QCryptoCryptoDevBackend *backend,
                           uint64_t session_id,
                           uint32_t queue_index, Error **errp);
    int (*do_sym_op)(QCryptoCryptoDevBackend *backend,
                     QCryptoCryptoDevBackendSymOpInfo *op_info,
                     uint32_t queue_index, Error **errp);
} QCryptoCryptoDevBackendClass;


struct QCryptoCryptoDevBackendClientState {
    char *model;
    char *name;
    char *info_str;
    unsigned int queue_index;
    QTAILQ_ENTRY(QCryptoCryptoDevBackendClientState) next;
};

struct QCryptoCryptoDevBackendPeers {
    QCryptoCryptoDevBackendClientState *ccs[MAX_CRYPTO_QUEUE_NUM];
    uint32_t queues;
};

struct QCryptoCryptoDevBackendConf {
    QCryptoCryptoDevBackendPeers peers;

    /* Supported service mask */
    uint32_t crypto_services;

    /* Detailed algorithms mask */
    uint32_t cipher_algo_l;
    uint32_t cipher_algo_h;
    uint32_t hash_algo;
    uint32_t mac_algo_l;
    uint32_t mac_algo_h;
    uint32_t asym_algo;
    uint32_t kdf_algo;
    uint32_t aead_algo;
    uint32_t primitive_algo;
};

struct QCryptoCryptoDevBackend {
    Object parent_obj;

    bool ready;
    QCryptoCryptoDevBackendConf conf;
};

/**
 * qcrypto_cryptodev_backend_new_client:
 * @model: the cryptodev backend model
 * @name: the cryptodev backend name, can be NULL
 *
 * Creates a new cryptodev backend client object
 * with the @name in the model @model.
 *
 * The returned object must be released with
 * qcrypto_cryptodev_backend_free_client() when no
 * longer required
 *
 * Returns: a new cryptodev backend client object
 */
QCryptoCryptoDevBackendClientState *
qcrypto_cryptodev_backend_new_client(const char *model,
                                    const char *name);
/**
 * qcrypto_cryptodev_backend_free_client:
 * @cc: the cryptodev backend client object
 *
 * Release the memory associated with @cc that
 * was previously allocated by qcrypto_cryptodev_backend_new_client()
 */
void qcrypto_cryptodev_backend_free_client(
                  QCryptoCryptoDevBackendClientState *cc);

/**
 * qcrypto_cryptodev_backend_cleanup:
 * @backend: the cryptodev backend object
 * @errp: pointer to a NULL-initialized error object
 *
 * Clean the resouce associated with @backend that realizaed
 * by the specific backend's init() callback
 */
void qcrypto_cryptodev_backend_cleanup(
           QCryptoCryptoDevBackend *backend,
           Error **errp);

/**
 * qcrypto_cryptodev_backend_sym_create_session:
 * @backend: the cryptodev backend object
 * @sess_info: parameters needed by session creating
 * @queue_index: queue index of cryptodev backend client
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a session for symmetric algorithms
 *
 * Returns: session id on success, or -1 on error
 */
int64_t qcrypto_cryptodev_backend_sym_create_session(
           QCryptoCryptoDevBackend *backend,
           QCryptoCryptoDevBackendSymSessionInfo *sess_info,
           uint32_t queue_index, Error **errp);

/**
 * qcrypto_cryptodev_backend_sym_close_session:
 * @backend: the cryptodev backend object
 * @session_id: the session id
 * @queue_index: queue index of cryptodev backend client
 * @errp: pointer to a NULL-initialized error object
 *
 * Close a session for symmetric algorithms which was previously
 * created by qcrypto_cryptodev_backend_sym_create_session()
 *
 * Returns: 0 on success, or Negative on error
 */
int qcrypto_cryptodev_backend_sym_close_session(
           QCryptoCryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index, Error **errp);

/**
 * qcrypto_cryptodev_backend_crypto_operation:
 * @backend: the cryptodev backend object
 * @opaque: pointer to a VirtIOCryptoReq object
 * @queue_index: queue index of cryptodev backend client
 * @errp: pointer to a NULL-initialized error object
 *
 * Do crypto operation, such as encryption and
 * decryption
 *
 * Returns: VIRTIO_CRYPTO_OK on success,
 *         or -VIRTIO_CRYPTO_* on error
 */
int qcrypto_cryptodev_backend_crypto_operation(
                 QCryptoCryptoDevBackend *backend,
                 void *opaque,
                 uint32_t queue_index, Error **errp);

#endif /* QCRYPTO_CRYPTODEV_H */
