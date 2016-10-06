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

#ifndef _QEMU_VIRTIO_CRYPTO_H
#define _QEMU_VIRTIO_CRYPTO_H

#include "standard-headers/linux/virtio_crypto.h"
#include "hw/virtio/virtio.h"
#include "sysemu/iothread.h"
#include "sysemu/cryptodev.h"


#define DEBUG_VIRTIO_CRYPTO 0

#define DPRINTF(fmt, ...) \
do { \
    if (DEBUG_VIRTIO_CRYPTO) { \
        fprintf(stderr, "virtio_crypto: " fmt, ##__VA_ARGS__); \
    } \
} while (0)


#define TYPE_VIRTIO_CRYPTO "virtio-crypto-device"
#define VIRTIO_CRYPTO(obj) \
        OBJECT_CHECK(VirtIOCrypto, (obj), TYPE_VIRTIO_CRYPTO)
#define VIRTIO_CRYPTO_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_CRYPTO)

/* This is the last element of the write scatter-gather list */
struct virtio_crypto_inhdr
{
    uint32_t status;
};

typedef struct VirtIOCryptoConf {
    CryptoDevBackend *cryptodev;

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
} VirtIOCryptoConf;

struct VirtIOCrypto;

typedef struct VirtIOCryptoReq {
    VirtQueueElement elem;
    /* flags of operation, such as type of algorithm */
    uint32_t flags;
    struct virtio_crypto_inhdr *in;
    struct iovec *in_iov; /* Head address of dest iovec */
    unsigned int in_num; /* Number of dest iovec */
    size_t in_len;
    VirtQueue *vq;
    struct VirtIOCrypto *vcrypto;
    union {
        CryptoDevBackendSymOpInfo *sym_op_info;
    } u;
} VirtIOCryptoReq;

typedef struct VirtIOCrypto {
    VirtIODevice parent_obj;

    VirtQueue *ctrl_vq;

    VirtIOCryptoConf conf;
    CryptoDevBackend *cryptodev;

    uint32_t max_queues;
    uint32_t status;

    int multiqueue;
    uint32_t curr_queues;
    size_t config_size;
} VirtIOCrypto;

#endif /* _QEMU_VIRTIO_CRYPTO_H */
