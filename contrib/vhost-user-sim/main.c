/*
 * vhost-user sim main application
 *
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Johannes Berg <johannes.berg@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */
#include <gmodule.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "main.h"
#include "cal.h"

static int unix_sock_new(const char *unix_fn)
{
    int sock;
    struct sockaddr_un un;
    size_t len;

    g_assert(unix_fn);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock <= 0) {
        perror("socket");
        g_assert(0);
        return -1;
    }

    un.sun_family = AF_UNIX;
    (void)snprintf(un.sun_path, sizeof(un.sun_path), "%s", unix_fn);
    len = sizeof(un.sun_family) + strlen(un.sun_path);

    (void)unlink(unix_fn);
    if (bind(sock, (struct sockaddr *)&un, len) < 0) {
        perror("bind");
        goto fail;
    }

    if (listen(sock, 1) < 0) {
        perror("listen");
        goto fail;
    }

    return sock;

fail:
    (void)close(sock);
    g_assert(0);
    return -1;
}

static gpointer thread_func(gpointer data)
{
    g_main_context_push_thread_default(g_main_loop_get_context(data));
    g_main_loop_run(data);
    return NULL;
}

static GThread *new_device_thread(GIOFunc cb, const char *socket,
                                  const char *name)
{
    GMainContext *ctx = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(ctx, FALSE);
    int lsock = unix_sock_new(socket);
    GIOChannel *chan = g_io_channel_unix_new(lsock);
    GSource *src = g_io_create_watch(chan, G_IO_IN);

    g_source_set_callback(src, G_SOURCE_FUNC(cb), NULL, NULL);
    g_source_attach(src, ctx);

    return g_thread_new(name, thread_func, loop);
}

int main(int argc, char **argv)
{
    char *time_socket = NULL, *net_socket = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "s:n:h")) != -1) {
        switch (opt) {
        case 's':
            time_socket = g_strdup(optarg);
            break;
        case 'n':
            net_socket = g_strdup(optarg);
            break;
        case 'h':
        default:
            printf("Usage: %s -s time-device-socket [-n net-device-socket] | [ -h ]\n",
                   argv[0]);
            return 0;
        }
    }

    g_assert(time_socket);
#define N_CLIENTS 2
    fprintf(stderr,
            "============ starting up simulation, requires %d clients ============\n",
            N_CLIENTS);

    calendar_init(N_CLIENTS);

    new_device_thread(simtime_client_connected, time_socket, "time");
    if (net_socket) {
        new_device_thread(vu_net_client_connected, net_socket, "net");
    }

    calendar_run();

    unlink(time_socket);
    if (net_socket)
        unlink(net_socket);

    return 0;
}
