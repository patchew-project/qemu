/*
 * Common utilities for testing ivshmem devices
 *
 * SPDX-FileCopyrightText: 2012 SUSE LINUX Products GmbH
 * SPDX-FileCopyrightText: 2021 Red Hat, Inc.
 * SPDX-FileCopyrightText: 2023 Linaro Ltd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QTEST_IVSHMEM_UTILS_H
#define QTEST_IVSHMEM_UTILS_H

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "contrib/ivshmem-server/ivshmem-server.h"
#include "libqtest.h"

enum Reg {
    INTRMASK = 0,
    INTRSTATUS = 4,
    IVPOSITION = 8,
    DOORBELL = 12,
};

enum ServerStartStatus {
    SERVER = 1, /* Ivshmem server started */
    THREAD = 2, /* Thread for monitoring fds created */
    PIPE = 4,   /* Pipe created */
};

typedef struct ServerThread {
    GThread *thread;
    IvshmemServer server;
    /*
     * Pipe is used to communicate with the thread, asking it to terminate on
     * receiving 'q'.
     */
    int pipe[2];
    /*
     * Server statuses are used to keep track of thread/server/pipe start since
     * test_ivshmem_server_stop can be called at any time on a test error,
     * even from test_ivshmem_server_start itself, therefore, they are used for
     * proper service termination.
     */
    enum ServerStartStatus status;
} ServerThread;

gchar *mktempshm(int size, int *fd);
gchar *mktempsocket(void);
void test_ivshmem_server_start(ServerThread *thread,
                               const char *server_socket_path,
                               const char *shm_rel_path, unsigned num_vectors);
void test_ivshmem_server_stop(ServerThread *thread);

#endif /* QTEST_IVSHMEM_UTILS_H */
