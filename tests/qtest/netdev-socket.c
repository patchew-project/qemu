/*
 * QTest testcase for netdev stream and dgram
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"

#define CONNECTION_TIMEOUT    5

#define EXPECT_STATE(q, e, t)                             \
do {                                                      \
    char *resp = qtest_hmp(q, "info network");            \
    if (t) {                                              \
        strrchr(resp, t)[0] = 0;                          \
    }                                                     \
    g_test_timer_start();                                 \
    while (g_test_timer_elapsed() < CONNECTION_TIMEOUT) { \
        if (strcmp(resp, e) == 0) {                       \
            break;                                        \
        }                                                 \
        g_free(resp);                                     \
        resp = qtest_hmp(q, "info network");              \
        if (t) {                                          \
            strrchr(resp, t)[0] = 0;                      \
        }                                                 \
    }                                                     \
    g_assert_cmpstr(resp, ==, e);                         \
    g_free(resp);                                         \
} while (0)

static int inet_get_free_port(void)
{
    int sock;
    struct sockaddr_in addr;
    socklen_t len;
    int port;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return -1;
    }

    len = sizeof(addr);
    if (getsockname(sock,  (struct sockaddr *)&addr, &len) < 0) {
        return -1;
    }

    port = ntohs(addr.sin_port);

    close(sock);

    return port;
}

static void test_stream_inet(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int port;

    port = inet_get_free_port();
    qts0 = qtest_initf("-nodefaults "
                       "-netdev stream,id=st0,addr.type=inet,"
                       "addr.host=localhost,addr.port=%d", port);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults "
                       "-netdev stream,server=false,id=st0,addr.type=inet,"
                       "addr.host=localhost,addr.port=%d", port);

    expect = g_strdup_printf("st0: index=0,type=stream,tcp:127.0.0.1:%d\r\n",
                             port);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    /* the port is unknown, check only the address */
    EXPECT_STATE(qts0, "st0: index=0,type=stream,tcp:127.0.0.1", ':');

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_stream_unix(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    gchar *path;
    int ret;

    ret = g_file_open_tmp("netdev-XXXXXX", &path, NULL);
    g_assert_true(ret >= 0);
    close(ret);

    qts0 = qtest_initf("-nodefaults "
                       "-netdev stream,id=st0,addr.type=unix,addr.path=%s",
                       path);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults "
                       "-netdev stream,id=st0,server=false,addr.type=unix,addr.path=%s",
                       path);

    expect = g_strdup_printf("st0: index=0,type=stream,unix:%s\r\n", path);
    EXPECT_STATE(qts1, expect, 0);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);
    g_free(path);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_stream_fd(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int ret, sock0, sock1;
    struct sockaddr_un addr;
    gchar *path;

    ret = g_file_open_tmp("netdev-XXXXXX", &path, NULL);
    g_assert_true(ret >= 0);
    close(ret);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    unlink(addr.sun_path);
    sock0 = socket(AF_LOCAL, SOCK_STREAM, 0);
    g_assert_cmpint(sock0, !=, -1);

    ret = bind(sock0, (struct sockaddr *)&addr, sizeof(addr));
    g_assert_cmpint(ret, !=, -1);

    qts0 = qtest_initf("-nodefaults "
                       "-netdev stream,id=st0,addr.type=fd,addr.str=%d",
                       sock0);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    sock1 = socket(AF_LOCAL, SOCK_STREAM, 0);
    g_assert_cmpint(sock1, !=, -1);

    ret = connect(sock1, (struct sockaddr *)&addr, sizeof(addr));
    g_assert_cmpint(ret, !=, -1);

    qts1 = qtest_initf("-nodefaults "
                       "-netdev stream,id=st0,server=off,addr.type=fd,addr.str=%d",
                       sock1);


    expect = g_strdup_printf("st0: index=0,type=stream,unix:%s\r\n", path);
    EXPECT_STATE(qts1, expect, 0);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qtest_quit(qts1);
    qtest_quit(qts0);

    closesocket(sock0);
    closesocket(sock1);

    g_free(path);
}

static void test_dgram_inet(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int port0, port1;

    port0 = inet_get_free_port();
    port1 = inet_get_free_port();

    qts0 = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,"
                       "local.type=inet,local.host=localhost,local.port=%d,"
                       "remote.type=inet,remote.host=localhost,remote.port=%d",
                        port0, port1);

    expect = g_strdup_printf("st0: index=0,type=dgram,"
                             "udp=127.0.0.1:%d/127.0.0.1:%d\r\n", port0, port1);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qts1 = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,"
                       "local.type=inet,local.host=localhost,local.port=%d,"
                       "remote.type=inet,remote.host=localhost,remote.port=%d",
                        port1, port0);

    expect = g_strdup_printf("st0: index=0,type=dgram,"
                             "udp=127.0.0.1:%d/127.0.0.1:%d\r\n", port1, port0);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_dgram_mcast(void)
{
    QTestState *qts;

    qts = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,"
                       "remote.type=inet,remote.host=230.0.0.1,remote.port=1234");

    EXPECT_STATE(qts, "st0: index=0,type=dgram,mcast=230.0.0.1:1234\r\n", 0);

    qtest_quit(qts);
}

static void test_dgram_unix(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    gchar *path0, *path1;
    int ret;

    ret = g_file_open_tmp("netdev-XXXXXX", &path0, NULL);
    g_assert_true(ret >= 0);
    close(ret);

    ret = g_file_open_tmp("netdev-XXXXXX", &path1, NULL);
    g_assert_true(ret >= 0);
    close(ret);

    qts0 = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,local.type=unix,local.path=%s,"
                       "remote.type=unix,remote.path=%s",
                       path0, path1);

    expect = g_strdup_printf("st0: index=0,type=dgram,udp=%s:%s\r\n",
                             path0, path1);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qts1 = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,local.type=unix,local.path=%s,"
                       "remote.type=unix,remote.path=%s",
                       path1, path0);


    expect = g_strdup_printf("st0: index=0,type=dgram,udp=%s:%s\r\n",
                             path1, path0);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    g_free(path0);
    g_free(path1);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_dgram_fd(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int ret;
    int sv[2];

    ret = socketpair(PF_UNIX, SOCK_DGRAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    qts0 = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,local.type=fd,local.str=%d",
                       sv[0]);

    expect = g_strdup_printf("st0: index=0,type=dgram,fd=%d unix\r\n", sv[0]);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qts1 = qtest_initf("-nodefaults "
                       "-netdev dgram,id=st0,local.type=fd,local.str=%d",
                       sv[1]);


    expect = g_strdup_printf("st0: index=0,type=dgram,fd=%d unix\r\n", sv[1]);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    qtest_quit(qts1);
    qtest_quit(qts0);

    close(sv[0]);
    close(sv[1]);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/netdev/stream/inet", test_stream_inet);
    qtest_add_func("/netdev/stream/unix", test_stream_unix);
    qtest_add_func("/netdev/stream/fd", test_stream_fd);
    qtest_add_func("/netdev/dgram/inet", test_dgram_inet);
    qtest_add_func("/netdev/dgram/mcast", test_dgram_mcast);
    qtest_add_func("/netdev/dgram/unix", test_dgram_unix);
    qtest_add_func("/netdev/dgram/fd", test_dgram_fd);

    ret = g_test_run();

    return ret;
}
