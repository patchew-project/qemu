/*
 * vfio-user proxy tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "hw/vfio/vfio-device.h"
#include "hw/vfio-user/proxy.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "system/iothread.h"
#include "iothread.h"

typedef enum TestReplyMode {
    TEST_REPLY_SUCCESS,
    TEST_REPLY_PRIOR_ERROR,
    TEST_REPLY_TIMEOUT,
} TestReplyMode;

typedef struct TestServer {
    char *tmpdir;
    char *path;
    int listen_fd;
    TestReplyMode reply_mode;
    GThread *thread;
} TestServer;

IOThread *iothread_create(const char *id, Error **errp)
{
    (void)id;
    (void)errp;
    return iothread_new();
}

void iothread_destroy(IOThread *iothread)
{
    iothread_join(iothread);
}

static void read_all(int fd, void *buf, size_t len)
{
    char *p = buf;

    while (len) {
        ssize_t ret = read(fd, p, len);

        if (ret < 0 && errno == EINTR) {
            continue;
        }
        g_assert_cmpint(ret, >, 0);
        p += ret;
        len -= ret;
    }
}

static void write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len) {
        ssize_t ret = write(fd, p, len);

        if (ret < 0 && errno == EINTR) {
            continue;
        }
        g_assert_cmpint(ret, >, 0);
        p += ret;
        len -= ret;
    }
}

static gpointer test_server_thread(gpointer opaque)
{
    TestServer *server = opaque;
    int conn_fd;
    int i;

    do {
        conn_fd = accept(server->listen_fd, NULL, NULL);
    } while (conn_fd < 0 && errno == EINTR);
    g_assert_cmpint(conn_fd, >=, 0);

    for (i = 0; i < 2; i++) {
        VFIOUserHdr request;
        VFIOUserHdr reply;

        read_all(conn_fd, &request, sizeof(request));
        g_assert_cmpuint(request.size, ==, sizeof(request));

        reply = request;
        reply.size = sizeof(reply);
        reply.flags = VFIO_USER_REPLY;
        reply.error_reply = 0;
        if (i == 0 && server->reply_mode == TEST_REPLY_PRIOR_ERROR) {
            reply.flags |= VFIO_USER_ERROR;
            reply.error_reply = EIO;
        }
        if (server->reply_mode != TEST_REPLY_TIMEOUT) {
            write_all(conn_fd, &reply, sizeof(reply));
        }
    }

    while (read(conn_fd, &i, sizeof(i)) < 0 && errno == EINTR) {
        ;
    }

    close(conn_fd);
    return NULL;
}

static void test_server_start(TestServer *server, TestReplyMode reply_mode)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };

    server->tmpdir = g_dir_make_tmp("qemu-test-vfio-user.XXXXXX", NULL);
    g_assert_nonnull(server->tmpdir);
    server->path = g_build_filename(server->tmpdir, "socket", NULL);
    g_assert_cmpuint(strlen(server->path), <, sizeof(addr.sun_path));
    pstrcpy(addr.sun_path, sizeof(addr.sun_path), server->path);

    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(server->listen_fd, >=, 0);
    g_assert_no_errno(bind(server->listen_fd, (struct sockaddr *)&addr,
                           sizeof(addr)));
    g_assert_no_errno(listen(server->listen_fd, 1));

    server->reply_mode = reply_mode;
    server->thread = g_thread_new("vfio-user-test-server",
                                  test_server_thread, server);
}

static void test_server_stop(TestServer *server)
{
    g_thread_join(server->thread);
    close(server->listen_fd);
    g_assert_cmpint(g_unlink(server->path), ==, 0);
    g_assert_cmpint(g_rmdir(server->tmpdir), ==, 0);
    g_free(server->path);
    g_free(server->tmpdir);
}

static void unexpected_request(void *opaque, VFIOUserMsg *msg)
{
    (void)opaque;
    (void)msg;
    g_assert_not_reached();
}

static void send_nowait(VFIOUserProxy *proxy)
{
    VFIOUserHdr *hdr = g_new0(VFIOUserHdr, 1);

    vfio_user_request_msg(hdr, VFIO_USER_DMA_UNMAP, sizeof(*hdr), 0);
    g_assert_true(vfio_user_send_nowait(proxy, hdr, NULL, 0, &error_abort));
}

static void test_nowait_replies(gconstpointer opaque)
{
    TestReplyMode reply_mode = GPOINTER_TO_INT(opaque);
    TestServer server = { 0 };
    SocketAddress addr = { .type = SOCKET_ADDRESS_TYPE_UNIX };
    VFIOUserProxy *proxy;
    VFIODevice vbasedev = { 0 };
    Error *err = NULL;

    test_server_start(&server, reply_mode);
    addr.u.q_unix.path = server.path;

    proxy = vfio_user_connect_dev(&addr, &error_abort);
    proxy->wait_time = reply_mode == TEST_REPLY_TIMEOUT ? 50 : 5000;
    vbasedev.proxy = proxy;
    vfio_user_set_handler(&vbasedev, unexpected_request, NULL);

    send_nowait(proxy);
    send_nowait(proxy);

    if (reply_mode == TEST_REPLY_PRIOR_ERROR) {
        g_assert_false(vfio_user_wait_reqs(proxy, &err));
        g_assert_nonnull(err);
        g_assert_nonnull(strstr(error_get_pretty(err), strerror(EIO)));
        error_free(err);
    } else if (reply_mode == TEST_REPLY_TIMEOUT) {
        g_assert_false(vfio_user_wait_reqs(proxy, &err));
        g_assert_nonnull(err);
        g_assert_nonnull(strstr(error_get_pretty(err), "timed out"));
        error_free(err);
    } else {
        g_assert_true(vfio_user_wait_reqs(proxy, &err));
        g_assert_null(err);
    }

    vfio_user_disconnect(proxy);
    test_server_stop(&server);
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    qemu_init_main_loop(&error_abort);
    socket_init();
    g_test_init(&argc, &argv, NULL);

    g_test_add_data_func("/vfio-user/proxy/nowait/prior-error",
                         GINT_TO_POINTER(TEST_REPLY_PRIOR_ERROR),
                         test_nowait_replies);
    g_test_add_data_func("/vfio-user/proxy/nowait/timeout",
                         GINT_TO_POINTER(TEST_REPLY_TIMEOUT),
                         test_nowait_replies);
    g_test_add_data_func("/vfio-user/proxy/nowait/success",
                         GINT_TO_POINTER(TEST_REPLY_SUCCESS),
                         test_nowait_replies);

    return g_test_run();
}
