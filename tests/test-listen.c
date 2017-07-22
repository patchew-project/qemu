/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 *    Author: Knut Omang <knut.omang@oracle.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * Test parallel port listen configuration with
 * dynamic port allocation
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"
#include "qapi/error.h"

#define NAME_LEN 1024
#define PORT_LEN 16

struct thr_info {
    QemuThread thread;
    int to_port;
    bool ipv4;
    bool ipv6;
    int got_port;
    int eno;
    int fd;
    const char *errstr;
    char hostname[NAME_LEN + 1];
    char port[PORT_LEN + 1];
};


/* These two functions taken from test-io-channel-socket.c */
static int check_bind(const char *hostname, bool *has_proto)
{
    int fd = -1;
    struct addrinfo ai, *res = NULL;
    int rc;
    int ret = -1;

    memset(&ai, 0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = AF_UNSPEC;
    ai.ai_socktype = SOCK_STREAM;

    /* lookup */
    rc = getaddrinfo(hostname, NULL, &ai, &res);
    if (rc != 0) {
        if (rc == EAI_ADDRFAMILY ||
            rc == EAI_FAMILY) {
            *has_proto = false;
            goto done;
        }
        goto cleanup;
    }

    fd = qemu_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        goto cleanup;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
        if (errno == EADDRNOTAVAIL) {
            *has_proto = false;
            goto done;
        }
        goto cleanup;
    }

    *has_proto = true;
 done:
    ret = 0;

 cleanup:
    if (fd != -1) {
        close(fd);
    }
    if (res) {
        freeaddrinfo(res);
    }
    return ret;
}

static int check_protocol_support(bool *has_ipv4, bool *has_ipv6)
{
    if (check_bind("127.0.0.1", has_ipv4) < 0) {
        return -1;
    }
    if (check_bind("::1", has_ipv6) < 0) {
        return -1;
    }

    return 0;
}

static void *listener_thread(void *arg)
{
    struct thr_info *thr = (struct thr_info *)arg;
    SocketAddress addr = {
        .type = SOCKET_ADDRESS_TYPE_INET,
        .u = {
            .inet = {
                .host = thr->hostname,
                .port = thr->port,
                .has_ipv4 = thr->ipv4,
                .ipv4 = thr->ipv4,
                .has_ipv6 = thr->ipv6,
                .ipv6 = thr->ipv6,
                .has_to = true,
                .to = thr->to_port,
            },
        },
    };
    Error *err = NULL;
    int fd;

    fd = socket_listen(&addr, &err);
    if (fd < 0) {
        thr->eno = errno;
        thr->errstr = error_get_pretty(err);
    } else {
        struct sockaddr_in a;
        socklen_t a_len = sizeof(a);
        g_assert_cmpint(getsockname(fd, (struct sockaddr *)&a, &a_len), ==, 0);
        thr->got_port = ntohs(a.sin_port);
        thr->fd = fd;
    }
    return arg;
}


static void listen_compete_nthr(bool threaded, int nthreads,
                                int start_port, int max_offset,
                                bool ipv4, bool ipv6)
{
    int i;
    int failed_listens = 0;
    struct thr_info *thr = g_new0(struct thr_info, nthreads);
    int used[max_offset + 1];

    memset(used, 0, sizeof(used));
    for (i = 0; i < nthreads; i++) {
        snprintf(thr[i].port, PORT_LEN, "%d", start_port);
        strcpy(thr[i].hostname, "localhost");
        thr[i].to_port = start_port + max_offset;
        thr[i].ipv4 = ipv4;
        thr[i].ipv6 = ipv6;
    }

    for (i = 0; i < nthreads; i++) {
        if (threaded) {
            qemu_thread_create(&thr[i].thread, "listener",
                               listener_thread, &thr[i],
                               QEMU_THREAD_JOINABLE);
        } else {
            listener_thread(&thr[i]);
        }
    }

    if (threaded) {
        for (i = 0; i < nthreads; i++) {
            qemu_thread_join(&thr[i].thread);
        }
    }
    for (i = 0; i < nthreads; i++) {
        if (thr[i].got_port) {
            closesocket(thr[i].fd);
        }
    }

    for (i = 0; i < nthreads; i++) {
        if (thr[i].eno != 0) {
            const char *m;
            g_printerr("** Failed to assign a port to thread %d (errno = %d)\n",
                   i, thr[i].eno);
            /* This is what we are interested in capturing -
             * catch and report details if something unexpected happens:
             */
            m = strstr(thr[i].errstr, "Failed to listen on socket");
            if (m != NULL) {
                g_assert_cmpstr(thr[i].errstr, ==,
                    "Failed to listen on socket: Address already in use");
            }
            failed_listens++;
        } else {
            int assigned_port = thr[i].got_port;
            g_assert_cmpint(assigned_port, <= , thr[i].to_port);
            g_assert_cmpint(used[assigned_port - start_port], == , 0);
        }
    }
    g_assert_cmpint(failed_listens, ==, 0);
    g_free(thr);
}


static void listen_compete_ipv4(void)
{
    listen_compete_nthr(true, 200, 5920, 300, true, false);
}

static void listen_serial_ipv4(void)
{
    listen_compete_nthr(false, 200, 6300, 300, true, false);
}

static void listen_compete_ipv6(void)
{
    listen_compete_nthr(true, 200, 5920, 300, true, false);
}

static void listen_serial_ipv6(void)
{
    listen_compete_nthr(false, 200, 6300, 300, false, true);
}

static void listen_compete_gen(void)
{
    listen_compete_nthr(true, 200, 5920, 300, true, true);
}

static void listen_serial_gen(void)
{
    listen_compete_nthr(false, 200, 6300, 300, true, true);
}


int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;
    g_test_init(&argc, &argv, NULL);

    if (check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        return 1;
    }

    if (has_ipv4) {
        g_test_add_func("/socket/listen-serial/ipv4", listen_serial_ipv4);
        g_test_add_func("/socket/listen-compete/ipv4", listen_compete_ipv4);
    }
    if (has_ipv6) {
        g_test_add_func("/socket/listen-serial/ipv6", listen_serial_ipv6);
        g_test_add_func("/socket/listen-compete/ipv6", listen_compete_ipv6);
    }
    if (has_ipv4 && has_ipv6) {
        g_test_add_func("/socket/listen-serial/generic", listen_serial_gen);
        g_test_add_func("/socket/listen-compete/generic", listen_compete_gen);
    }
    return g_test_run();
}
