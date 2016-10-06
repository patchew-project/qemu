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


typedef struct QCryptoCryptoDevBackendClass {
    ObjectClass parent_class;

    void (*init)(QCryptoCryptoDevBackend *backend, Error **errp);
    void (*cleanup)(QCryptoCryptoDevBackend *backend, Error **errp);
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

#endif /* QCRYPTO_CRYPTODEV_H */
