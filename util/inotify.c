/*
 * QEMU inotify helper
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/inotify.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

struct QInotify {
    int fd;
    QInotifyHandler cb;
    void *opaque;
    GDestroyNotify ffcb;
};

#ifdef CONFIG_INOTIFY1
static void qemu_inotify_watch(void *arg)
{
    QInotify *in = arg;
    char buf[4096]
        __attribute__ ((aligned(__alignof__(struct inotify_event))));
    int used = 0;
    int len = read(in->fd, buf, sizeof(buf));

    if (len < 0) {
        if (errno != EAGAIN) {
            error_report("Failure monitoring inotify FD, disabling events");
            goto error;
        }

        /* no more events right now */
        return;
    }

    /* Loop over all events in the buffer */
    while (used < len) {
        struct inotify_event *ev =
            (struct inotify_event *)buf + used;

        in->cb(ev->wd, ev->mask, ev->name, in->opaque);

        used += sizeof(struct inotify_event) + ev->len;
    }

    return;

 error:
    qemu_set_fd_handler(in->fd, NULL, NULL, NULL);
    close(in->fd);
    in->fd = -1;
}
#endif

QInotify *qemu_inotify_new(QInotifyHandler cb,
                           void *opaque,
                           GDestroyNotify ffcb,
                           Error **errp)
{
#ifdef CONFIG_INOTIFY1
    QInotify *in = g_new0(QInotify, 1);

    in->fd = inotify_init1(IN_NONBLOCK);
    if (in->fd == -1) {
        error_setg_errno(errp, errno,
                         "Unable to initialize inotify");
        g_free(in);
        return NULL;
    }
    in->cb = cb;
    in->opaque = opaque;
    in->ffcb = ffcb;

    qemu_set_fd_handler(in->fd, qemu_inotify_watch, NULL, in);

    return in;
#else
    error_setg(errp, "Inotify not available on this platform");
    return NULL;
#endif

}


void qemu_inotify_free(QInotify *in)
{
    if (!in) {
        return;
    }

    if (in->ffcb) {
        in->ffcb(in->opaque);
    }

    if (in->fd != -1) {
        qemu_set_fd_handler(in->fd, NULL, NULL, NULL);
        close(in->fd);
        g_free(in);
    }
}


int qemu_inotify_add_watch(QInotify *in,
                           const char *path,
                           uint32_t mask,
                           Error **errp)
{
#ifdef CONFIG_INOTIFY1
    int rv;
    rv = inotify_add_watch(in->fd, path, mask);
    if (rv < 0) {
        error_setg_errno(errp, errno, "Unable to watch '%s'", path);
        return -1;
    }

    return rv;
#else
    error_setg(errp, "Inotify not available on this platform");
    return -1;
#endif
}
