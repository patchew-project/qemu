/*
 * Common utilities for testing ivshmem devices
 *
 * SPDX-FileCopyrightText: 2012 SUSE LINUX Products GmbH
 * SPDX-FileCopyrightText: 2021 Red Hat, Inc.
 * SPDX-FileCopyrightText: 2023 Linaro Ltd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "ivshmem-utils.h"

gchar *mktempshm(int size, int *fd)
{
    while (true) {
        /* Relative path to the shm filesystem, e.g. '/dev/shm'. */
        gchar *shm_rel_path;

        shm_rel_path = g_strdup_printf("/ivshmem_qtest-%u-%u", getpid(),
                                       g_test_rand_int());
        *fd = shm_open(shm_rel_path, O_CREAT | O_RDWR | O_EXCL,
                       S_IRWXU | S_IRWXG | S_IRWXO);
        if (*fd > 0) {
            g_assert(ftruncate(*fd, size) == 0);
            return shm_rel_path;
        }

        g_free(shm_rel_path);

        if (errno != EEXIST) {
            perror("shm_open");
            return NULL;
        }
    }
}

gchar *mktempsocket(void)
{
    gchar *server_socket_path;

    server_socket_path = g_strdup_printf("%s/ivshmem_socket_qtest-%u-%u",
                                         g_get_tmp_dir(), getpid(),
                                         g_test_rand_int());
    return server_socket_path;
}

static void *server_thread(void *data)
{
    ServerThread *t = data;
    IvshmemServer *server = &t->server;

    while (true) {
        fd_set fds;
        int maxfd, ret;

        FD_ZERO(&fds);
        FD_SET(t->pipe[0], &fds);
        maxfd = t->pipe[0] + 1;

        ivshmem_server_get_fds(server, &fds, &maxfd);

        ret = select(maxfd, &fds, NULL, NULL, NULL);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            g_critical("select error: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (FD_ISSET(t->pipe[0], &fds)) {
            break;
        }

        if (ivshmem_server_handle_fds(server, &fds, maxfd) < 0) {
            g_critical("ivshmem_server_handle_fds() failed\n");
            break;
        }
    }

    return NULL;
}

void test_ivshmem_server_start(ServerThread *thread,
                               const char *server_socket_path,
                               const char *shm_rel_path, unsigned num_vectors)
{
    g_autoptr(GError) err = NULL;
    int ret;
    struct stat shm_st;
    char *shm_path;

    g_assert(thread != NULL);
    g_assert(server_socket_path != NULL);
    g_assert_cmpint(num_vectors, >, 0);
    g_assert(shm_rel_path != NULL);

    /*
     * Find out shm size. shm_open() deals with relative paths but stat() needs
     * the full path to the shm file.
     */
    shm_path = g_strdup_printf("/dev/shm%s", shm_rel_path);
    ret = stat(shm_path, &shm_st);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(shm_st.st_size, >, 0);

    ret = ivshmem_server_init(&thread->server, server_socket_path, shm_rel_path,
    true, shm_st.st_size, num_vectors, g_test_verbose());
    g_assert_cmpint(ret, ==, 0);
    ret = ivshmem_server_start(&thread->server);
    g_assert_cmpint(ret, ==, 0);
    thread->status = SERVER;

    g_unix_open_pipe(thread->pipe, FD_CLOEXEC, &err);
    g_assert_no_error(err);
    thread->status |= PIPE;

    thread->thread = g_thread_new("ivshmem-server", server_thread, thread);
    g_assert(thread->thread != NULL);
    thread->status |= THREAD;
}

void test_ivshmem_server_stop(ServerThread *thread)
{
    /*
     * This function can be called any time on a test error/abort (e.g., it can
     * be called from the abort handler), including from the
     * test_ivshmem_server_start(). Therefore, the start steps (server started,
     * pipe created, and thread created) are tracked when the server starts and
     * then checked below accordingly for proper termination.
     */

    if (thread->status & THREAD) {
        /* Ask to exit from thread. */
        if (qemu_write_full(thread->pipe[1], "q", 1) != 1) {
            g_error("qemu_write_full: %s", g_strerror(errno));
        }

        /* Wait thread to exit. */
        g_thread_join(thread->thread);
     }

    if (thread->status & PIPE)  {
        close(thread->pipe[1]);
        close(thread->pipe[0]);
    }

    if (thread->status & SERVER) {
        ivshmem_server_close(&thread->server);
    }
}
