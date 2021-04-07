/*
 * QEMU Block driver for  NBD
 *
 * Copyright (c) 2020 Virtuozzo International GmbH.
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

#include "qapi/qapi-visit-sockets.h"
#include "qapi/clone-visitor.h"

#include "block/nbd.h"

typedef struct NBDConnectThread {
    SocketAddress *saddr; /* address to connect to */
    NBDConnectThreadCallback cb;
    void *cb_opaque;
} NBDConnectThread;

static void *connect_thread_func(void *opaque)
{
    NBDConnectThread *thr = opaque;
    int ret;
    QIOChannelSocket *sioc = qio_channel_socket_new();

    ret = qio_channel_socket_connect_sync(sioc, thr->saddr, NULL);
    if (ret < 0) {
        object_unref(OBJECT(sioc));
        sioc = NULL;
    }

    thr->cb(sioc, ret, thr->cb_opaque);

    qapi_free_SocketAddress(thr->saddr);
    g_free(thr);

    return NULL;
}

void nbd_connect_thread_start(const SocketAddress *saddr,
                              NBDConnectThreadCallback cb, void *cb_opaque)
{
    QemuThread thread;
    NBDConnectThread *thr = g_new(NBDConnectThread, 1);

    *thr = (NBDConnectThread) {
        .saddr = QAPI_CLONE(SocketAddress, saddr),
        .cb = cb,
        .cb_opaque = cb_opaque,
    };

    qemu_thread_create(&thread, "nbd-connect",
                       connect_thread_func, thr, QEMU_THREAD_DETACHED);
}
