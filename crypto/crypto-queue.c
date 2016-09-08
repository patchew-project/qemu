/*
 * Queue management for crypto device (based on net/qeueu.c)
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "crypto/crypto-queue.h"
#include "crypto/crypto.h"
#include "qemu/queue.h"


/* The delivery handler may only return zero if it will call
 * qemu_crypto_queue_flush() when it determines that it is once again able
 * to deliver packets. It must also call qemu_crypto_queue_purge() in its
 * cleanup path.
 *
 * If a sent callback is provided to send(), the caller must handle a
 * zero return from the delivery handler by not sending any more packets
 * until we have invoked the callback. Only in that case will we queue
 * the packet.
 *
 * If a sent callback isn't provided, we just drop the packet to avoid
 * unbounded queueing.
 */

struct CryptoPacket {
    QTAILQ_ENTRY(CryptoPacket) entry;
    CryptoClientState *sender;
    unsigned flags; /* algorithms' type etc. */
    CryptoPacketSent *sent_cb; /* callback after packet sent */
    void *opaque; /* header struct pointer of operation */
    uint8_t data[0];
};

struct CryptoQueue {
    void *opaque;
    uint32_t nq_maxlen;
    uint32_t nq_count;
    CryptoQueueDeliverFunc *deliver;

    QTAILQ_HEAD(packets, CryptoPacket) packets;

    unsigned delivering:1;
};

CryptoQueue *
qemu_new_crypto_queue(CryptoQueueDeliverFunc *deliver, void *opaque)
{
    CryptoQueue *queue;

    queue = g_new0(CryptoQueue, 1);

    queue->opaque = opaque;
    queue->nq_maxlen = 10000;
    queue->nq_count = 0;
    queue->deliver = deliver;

    QTAILQ_INIT(&queue->packets);

    queue->delivering = 0;

    return queue;
}

void qemu_del_crypto_queue(CryptoQueue *queue)
{
    CryptoPacket *packet, *next;

    QTAILQ_FOREACH_SAFE(packet, &queue->packets, entry, next) {
        QTAILQ_REMOVE(&queue->packets, packet, entry);
        g_free(packet);
    }

    g_free(queue);
}

void qemu_crypto_queue_cache(CryptoQueue *queue,
                               unsigned flags,
                               CryptoClientState *sender,
                               void *opaque,
                               CryptoPacketSent *sent_cb)
{
    CryptoPacket *packet;

    if (queue->nq_count >= queue->nq_maxlen && !sent_cb) {
        return; /* drop if queue full and no callback */
    }

    packet = g_malloc(sizeof(CryptoPacket));
    packet->sender = sender;
    packet->sent_cb = sent_cb;
    packet->flags = flags,
    packet->opaque = opaque;

    queue->nq_count++;
    QTAILQ_INSERT_TAIL(&queue->packets, packet, entry);
}

static ssize_t qemu_crypto_queue_deliver(CryptoQueue *queue,
                                         unsigned flags,
                                         CryptoClientState *sender,
                                         void *opaque)
{
    ssize_t ret = -1;

    queue->delivering = 1;
    ret = queue->deliver(sender, flags, opaque, queue->opaque);
    queue->delivering = 0;

    return ret;
}

int qemu_crypto_queue_send(CryptoQueue *queue,
                            unsigned flags,
                            CryptoClientState *sender,
                            void *opaque,
                            CryptoPacketSent *sent_cb)
{
    int ret;

    if (queue->delivering) {
        qemu_crypto_queue_cache(queue, flags, sender,
                                opaque, sent_cb);
        return 0;
    }

    ret = qemu_crypto_queue_deliver(queue, flags, sender, opaque);
    if (ret == 0) {
        qemu_crypto_queue_cache(queue, flags, sender,
                                opaque, sent_cb);
        return 0;
    }

    qemu_crypto_queue_flush(queue);

    return ret;
}

void qemu_crypto_queue_purge(CryptoQueue *queue, CryptoClientState *from)
{
    CryptoPacket *packet, *next;

    QTAILQ_FOREACH_SAFE(packet, &queue->packets, entry, next) {
        if (packet->sender == from) {
            QTAILQ_REMOVE(&queue->packets, packet, entry);
            queue->nq_count--;
            if (packet->sent_cb) {
                packet->sent_cb(packet->sender, 0);
            }
            g_free(packet->opaque);
            g_free(packet);
        }
    }
}

bool qemu_crypto_queue_flush(CryptoQueue *queue)
{
    while (!QTAILQ_EMPTY(&queue->packets)) {
        CryptoPacket *packet;
        int ret;

        packet = QTAILQ_FIRST(&queue->packets);
        QTAILQ_REMOVE(&queue->packets, packet, entry);
        queue->nq_count--;

        ret = qemu_crypto_queue_deliver(queue, packet->flags,
                                        packet->sender, packet->opaque);
        if (ret == 0) {
            queue->nq_count++;
            QTAILQ_INSERT_HEAD(&queue->packets, packet, entry);
            return false;
        }

        if (packet->sent_cb) {
            packet->sent_cb(packet->sender, ret);
        }

        g_free(packet->opaque);
        g_free(packet);
    }
    return true;
}
