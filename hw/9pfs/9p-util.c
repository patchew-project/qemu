/*
 * 9p utilities
 *
 * Copyright IBM, Corp. 2017
 *
 * Authors:
 *  Greg Kurz <groug@kaod.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "9p-util.h"

int openat_nofollow(int dirfd, const char *path, int flags, mode_t mode)
{
    const char *tail = path;
    const char *c;
    int fd;

    fd = dup(dirfd);
    if (fd == -1) {
        return -1;
    }

    while (*tail) {
        int next_fd;
        char *head;

        while (*tail == '/') {
            tail++;
        }

        if (!*tail) {
            break;
        }

        head = g_strdup(tail);
        c = strchr(tail, '/');
        if (c) {
            head[c - tail] = 0;
            next_fd = openat(fd, head, O_DIRECTORY | O_RDONLY | O_NOFOLLOW);
        } else {
            /* We don't want bad things to happen like opening a file that
             * sits outside the virtfs export, or hanging on a named pipe,
             * or changing the controlling process of a terminal.
             */
            flags |= O_NOFOLLOW | O_NONBLOCK | O_NOCTTY;
            next_fd = openat(fd, head, flags, mode);
        }
        g_free(head);
        if (next_fd == -1) {
            close_preserve_errno(fd);
            return -1;
        }
        close(fd);
        fd = next_fd;

        if (!c) {
            break;
        }
        tail = c + 1;
    }
    /* O_NONBLOCK was only needed to open the file. Let's drop it. */
    assert(!fcntl(fd, F_SETFL, flags));

    return fd;
}
