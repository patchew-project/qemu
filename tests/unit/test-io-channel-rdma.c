/*
 * QEMU I/O channel RDMA test
 *
 * Copyright (c) 2024 HUAWEI TECHNOLOGIES CO., LTD.
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

#include "qemu/osdep.h"
#include "io/channel-rdma.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "io-channel-helpers.h"
#include "qapi-types-sockets.h"
#include <rdma/rsocket.h>

static SocketAddress *l_addr;
static SocketAddress *c_addr;

static void test_io_channel_set_rdma_bufs(QIOChannel *src, QIOChannel *dst)
{
    int buflen = 64 * 1024;

    /*
     * Make the socket buffers small so that we see
     * the effects of partial reads/writes
     */
    rsetsockopt(((QIOChannelRDMA *)src)->fd, SOL_SOCKET, SO_SNDBUF,
                (char *)&buflen, sizeof(buflen));

    rsetsockopt(((QIOChannelRDMA *)dst)->fd, SOL_SOCKET, SO_SNDBUF,
                (char *)&buflen, sizeof(buflen));
}

static void test_io_channel_setup_sync(InetSocketAddress *listen_addr,
                                       InetSocketAddress *connect_addr,
                                       QIOChannel **srv, QIOChannel **src,
                                       QIOChannel **dst)
{
    QIOChannelRDMA *lioc;

    lioc = qio_channel_rdma_new();
    qio_channel_rdma_listen_sync(lioc, listen_addr, 1, &error_abort);

    *src = QIO_CHANNEL(qio_channel_rdma_new());
    qio_channel_rdma_connect_sync(QIO_CHANNEL_RDMA(*src), connect_addr,
                                  &error_abort);
    qio_channel_set_delay(*src, false);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    *dst = QIO_CHANNEL(qio_channel_rdma_accept(lioc, &error_abort));
    g_assert(*dst);

    test_io_channel_set_rdma_bufs(*src, *dst);

    *srv = QIO_CHANNEL(lioc);
}

struct TestIOChannelData {
    bool err;
    GMainLoop *loop;
};

static void test_io_channel_complete(QIOTask *task, gpointer opaque)
{
    struct TestIOChannelData *data = opaque;
    data->err = qio_task_propagate_error(task, NULL);
    g_main_loop_quit(data->loop);
}

static void test_io_channel_setup_async(InetSocketAddress *listen_addr,
                                        InetSocketAddress *connect_addr,
                                        QIOChannel **srv, QIOChannel **src,
                                        QIOChannel **dst)
{
    QIOChannelRDMA *lioc;
    struct TestIOChannelData data;

    data.loop = g_main_loop_new(g_main_context_default(), TRUE);

    lioc = qio_channel_rdma_new();
    qio_channel_rdma_listen_async(lioc, listen_addr, 1,
                                  test_io_channel_complete, &data, NULL, NULL);

    g_main_loop_run(data.loop);
    g_main_context_iteration(g_main_context_default(), FALSE);

    g_assert(!data.err);

    *src = QIO_CHANNEL(qio_channel_rdma_new());

    qio_channel_rdma_connect_async(QIO_CHANNEL_RDMA(*src), connect_addr,
                                   test_io_channel_complete, &data, NULL, NULL);

    g_main_loop_run(data.loop);
    g_main_context_iteration(g_main_context_default(), FALSE);

    g_assert(!data.err);

    if (qemu_in_coroutine()) {
        qio_channel_yield(QIO_CHANNEL(lioc), G_IO_IN);
    } else {
        qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    }
    *dst = QIO_CHANNEL(qio_channel_rdma_accept(lioc, &error_abort));
    g_assert(*dst);

    qio_channel_set_delay(*src, false);
    test_io_channel_set_rdma_bufs(*src, *dst);

    *srv = QIO_CHANNEL(lioc);

    g_main_loop_unref(data.loop);
}

static void test_io_channel(bool async, InetSocketAddress *listen_addr,
                            InetSocketAddress *connect_addr)
{
    QIOChannel *src, *dst, *srv;
    QIOChannelTest *test;

    if (async) {
        /* async + blocking */

        test_io_channel_setup_async(listen_addr, connect_addr, &srv, &src,
                                    &dst);

        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, true, src, dst);
        qio_channel_test_validate(test);

        /* unref without close, to ensure finalize() cleans up */

        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));
        object_unref(OBJECT(srv));

        /* async + non-blocking */

        test_io_channel_setup_async(listen_addr, connect_addr, &srv, &src,
                                    &dst);

        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, false, src, dst);
        qio_channel_test_validate(test);

        /* close before unref, to ensure finalize copes with already closed */

        qio_channel_close(src, &error_abort);
        qio_channel_close(dst, &error_abort);
        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));

        qio_channel_close(srv, &error_abort);
        object_unref(OBJECT(srv));
    } else {
        /* sync + blocking */

        test_io_channel_setup_sync(listen_addr, connect_addr, &srv, &src, &dst);

        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, true, src, dst);
        qio_channel_test_validate(test);

        /* unref without close, to ensure finalize() cleans up */

        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));
        object_unref(OBJECT(srv));

        /* sync + non-blocking */

        test_io_channel_setup_sync(listen_addr, connect_addr, &srv, &src, &dst);

        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, false, src, dst);
        qio_channel_test_validate(test);

        /* close before unref, to ensure finalize copes with already closed */

        qio_channel_close(src, &error_abort);
        qio_channel_close(dst, &error_abort);
        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));

        qio_channel_close(srv, &error_abort);
        object_unref(OBJECT(srv));
    }
}

static void test_io_channel_rdma(bool async)
{
    InetSocketAddress *listen_addr;
    InetSocketAddress *connect_addr;

    listen_addr = &l_addr->u.inet;
    connect_addr = &l_addr->u.inet;

    test_io_channel(async, listen_addr, connect_addr);
}

static void test_io_channel_rdma_sync(void)
{
    test_io_channel_rdma(false);
}

static void test_io_channel_rdma_async(void)
{
    test_io_channel_rdma(true);
}

static void test_io_channel_rdma_co(void *opaque)
{
    test_io_channel_rdma(true);
}

static void test_io_channel_rdma_coroutine(void)
{
    Coroutine *coroutine;

    coroutine = qemu_coroutine_create(test_io_channel_rdma_co, NULL);
    qemu_coroutine_enter(coroutine);
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    qemu_init_main_loop(&error_abort);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s listen_addr connect_addr\n", argv[0]);
        exit(-1);
    }

    l_addr = socket_parse(argv[1], NULL);
    c_addr = socket_parse(argv[2], NULL);
    if (l_addr == NULL || c_addr == NULL ||
        l_addr->type != SOCKET_ADDRESS_TYPE_INET ||
        c_addr->type != SOCKET_ADDRESS_TYPE_INET) {
        fprintf(stderr, "Only socket address types 'inet' is supported\n");
        exit(-1);
    }

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/io/channel/rdma/sync", test_io_channel_rdma_sync);
    g_test_add_func("/io/channel/rdma/async", test_io_channel_rdma_async);
    g_test_add_func("/io/channel/rdma/coroutine",
                    test_io_channel_rdma_coroutine);

    return g_test_run();
}
