/*
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

#ifndef QEMU_CRYPTO_QUEUE_H
#define QEMU_CRYPTO_QUEUE_H

#include "qemu-common.h"

typedef struct CryptoPacket CryptoPacket;
typedef struct CryptoQueue CryptoQueue;
typedef struct CryptoPacketBuf CryptoPacketBuf;

typedef void (CryptoPacketSent) (CryptoClientState *, int);


/* Returns:
 *   >0 - success
 *    0 - queue packet for future redelivery
 *   <0 - failure (discard packet)
 */
typedef int (CryptoQueueDeliverFunc)(CryptoClientState *sender,
                                     unsigned flags,
                                     void *header_opaque,
                                     void *opaque);

CryptoQueue *
qemu_new_crypto_queue(CryptoQueueDeliverFunc *deliver, void *opaque);

void qemu_crypto_queue_cache(CryptoQueue *queue,
                               unsigned flags,
                               CryptoClientState *sender,
                               void *opaque,
                               CryptoPacketSent *sent_cb);

void qemu_del_crypto_queue(CryptoQueue *queue);

int qemu_crypto_queue_send(CryptoQueue *queue,
                                unsigned flags,
                                CryptoClientState *sender,
                                void *opaque,
                                CryptoPacketSent *sent_cb);

void qemu_crypto_queue_purge(CryptoQueue *queue, CryptoClientState *from);
bool qemu_crypto_queue_flush(CryptoQueue *queue);

#endif /* QEMU_CRYPTO_QUEUE_H */
