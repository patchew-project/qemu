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
#include "crypto/crypto.h"

#define VIRTIO_ID_CRYPTO 20

/* #define DEBUG_VIRTIO_CRYPTO */

#ifdef DEBUG_VIRTIO_CRYPTO
#define DPRINTF(fmt, ...) \
do { printf("virtio_crypto: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define TYPE_VIRTIO_CRYPTO "virtio-crypto-device"
#define VIRTIO_CRYPTO(obj) \
        OBJECT_CHECK(VirtIOCrypto, (obj), TYPE_VIRTIO_CRYPTO)
#define VIRTIO_CRYPTO_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_CRYPTO)


/* Limit the number of packets that can be sent via a single flush
 * of the TX queue.  This gives us a guaranteed exit condition and
 * ensures fairness in the io path.  256 conveniently matches the
 * length of the TX queue and shows a good balance of performance
 * and latency. */
#define VIRTIO_CRYPTO_TX_BURST 256

typedef struct VirtIOCryptoConf {
    int32_t txburst;
} VirtIOCryptoConf;

struct VirtIOCrypto;

typedef struct VirtIOCryptoQueue {
    VirtQueue *dataq;
    QEMUBH *tx_bh;
    int tx_waiting;
    struct {
        VirtQueueElement *elem;
        uint32_t flags;
        CryptoSymOpInfo *op_info;
        void *idata_hva;
    } async_tx;
    struct VirtIOCrypto *vcrypto;
} VirtIOCryptoQueue;

typedef struct VirtIOCrypto {
    VirtIODevice parent_obj;

    VirtIOCryptoQueue *vqs;
    VirtQueue *ctrl_vq;
    CryptoLegacyHWState *crypto;
    CryptoLegacyHWConf legacy_conf;

    VirtIOCryptoConf conf;
    int32_t tx_burst;
    uint32_t max_queues;
    uint32_t status;

    int multiqueue;
    uint32_t curr_queues;
    size_t config_size;
} VirtIOCrypto;

#endif /* _QEMU_VIRTIO_CRYPTO_H */
