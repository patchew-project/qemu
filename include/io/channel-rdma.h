/*
 * QEMU I/O channels RDMA driver
 *
 * Copyright (c) 2024 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *  Jialin Wang <wangjialin23@huawei.com>
 *  Gonglei <arei.gonglei@huawei.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QIO_CHANNEL_RDMA_H
#define QIO_CHANNEL_RDMA_H

#include "io/channel.h"
#include "io/task.h"
#include "qemu/sockets.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_RDMA "qio-channel-rdma"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelRDMA, QIO_CHANNEL_RDMA)

/**
 * QIOChannelRDMA:
 *
 * The QIOChannelRDMA object provides a channel implementation
 * that discards all writes and returns EOF for all reads.
 */
struct QIOChannelRDMA {
    QIOChannel parent;
    /* the rsocket fd */
    int fd;

    struct sockaddr_storage localAddr;
    socklen_t localAddrLen;
    struct sockaddr_storage remoteAddr;
    socklen_t remoteAddrLen;
};

/**
 * qio_channel_rdma_new:
 *
 * Create a channel for performing I/O on a rdma
 * connection, that is initially closed. After
 * creating the rdma, it must be setup as a client
 * connection or server.
 *
 * Returns: the rdma channel object
 */
QIOChannelRDMA *qio_channel_rdma_new(void);

/**
 * qio_channel_rdma_connect_sync:
 * @ioc: the rdma channel object
 * @addr: the address to connect to
 * @errp: pointer to a NULL-initialized error object
 *
 * Attempt to connect to the address @addr. This method
 * will run in the foreground so the caller will not regain
 * execution control until the connection is established or
 * an error occurs.
 */
int qio_channel_rdma_connect_sync(QIOChannelRDMA *ioc, InetSocketAddress *addr,
                                  Error **errp);

/**
 * qio_channel_rdma_connect_async:
 * @ioc: the rdma channel object
 * @addr: the address to connect to
 * @callback: the function to invoke on completion
 * @opaque: user data to pass to @callback
 * @destroy: the function to free @opaque
 * @context: the context to run the async task. If %NULL, the default
 *           context will be used.
 *
 * Attempt to connect to the address @addr. This method
 * will run in the background so the caller will regain
 * execution control immediately. The function @callback
 * will be invoked on completion or failure. The @addr
 * parameter will be copied, so may be freed as soon
 * as this function returns without waiting for completion.
 */
void qio_channel_rdma_connect_async(QIOChannelRDMA *ioc,
                                    InetSocketAddress *addr,
                                    QIOTaskFunc callback, gpointer opaque,
                                    GDestroyNotify destroy,
                                    GMainContext *context);

/**
 * qio_channel_rdma_listen_sync:
 * @ioc: the rdma channel object
 * @addr: the address to listen to
 * @num: the expected amount of connections
 * @errp: pointer to a NULL-initialized error object
 *
 * Attempt to listen to the address @addr. This method
 * will run in the foreground so the caller will not regain
 * execution control until the connection is established or
 * an error occurs.
 */
int qio_channel_rdma_listen_sync(QIOChannelRDMA *ioc, InetSocketAddress *addr,
                                 int num, Error **errp);

/**
 * qio_channel_rdma_listen_async:
 * @ioc: the rdma channel object
 * @addr: the address to listen to
 * @num: the expected amount of connections
 * @callback: the function to invoke on completion
 * @opaque: user data to pass to @callback
 * @destroy: the function to free @opaque
 * @context: the context to run the async task. If %NULL, the default
 *           context will be used.
 *
 * Attempt to listen to the address @addr. This method
 * will run in the background so the caller will regain
 * execution control immediately. The function @callback
 * will be invoked on completion or failure. The @addr
 * parameter will be copied, so may be freed as soon
 * as this function returns without waiting for completion.
 */
void qio_channel_rdma_listen_async(QIOChannelRDMA *ioc, InetSocketAddress *addr,
                                   int num, QIOTaskFunc callback,
                                   gpointer opaque, GDestroyNotify destroy,
                                   GMainContext *context);

/**
 * qio_channel_rdma_accept:
 * @ioc: the rdma channel object
 * @errp: pointer to a NULL-initialized error object
 *
 * If the rdma represents a server, then this accepts
 * a new client connection. The returned channel will
 * represent the connected client rdma.
 *
 * Returns: the new client channel, or NULL on error
 */
QIOChannelRDMA *qio_channel_rdma_accept(QIOChannelRDMA *ioc, Error **errp);

#endif /* QIO_CHANNEL_RDMA_H */
