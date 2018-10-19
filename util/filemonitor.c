/*
 * QEMU file_monitor helper
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
#include "qemu/filemonitor.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

struct QFileMonitor {
    QemuMutex lock;
    int fd;

    GHashTable *dirs; /* dirname => QFileMonitorDir */
    GHashTable *idmap; /* inotify ID => dirname */
};


typedef struct {
    int id; /* watch ID */
    char *filename; /* optional filter */
    QFileMonitorHandler cb;
    void *opaque;
} QFileMonitorWatch;


typedef struct {
    char *path;
    int id; /* inotify ID */
    int nextid; /* watch ID counter */
    gsize nwatches;
    QFileMonitorWatch *watches;
} QFileMonitorDir;


#ifdef CONFIG_INOTIFY1
#include <sys/inotify.h>

static void qemu_file_monitor_watch(void *arg)
{
    QFileMonitor *mon = arg;
    char buf[4096]
        __attribute__ ((aligned(__alignof__(struct inotify_event))));
    int used = 0;
    int len = read(mon->fd, buf, sizeof(buf));

    qemu_mutex_lock(&mon->lock);

    if (len < 0) {
        if (errno != EAGAIN) {
            error_report("Failure monitoring inotify FD, disabling events");
            goto cleanup;
        }

        /* no more events right now */
        goto cleanup;
    }

    /* Loop over all events in the buffer */
    while (used < len) {
        struct inotify_event *ev =
            (struct inotify_event *)(buf + used);
        const char *name = ev->len ? ev->name : "";
        QFileMonitorDir *dir = g_hash_table_lookup(mon->idmap,
                                                   GINT_TO_POINTER(ev->wd));
        uint32_t iev = ev->mask &
            (IN_CREATE | IN_MODIFY | IN_DELETE | IN_IGNORED |
             IN_MOVED_TO | IN_MOVED_FROM);
        int qev;
        gsize i;

        used += sizeof(struct inotify_event) + ev->len;

        if (!dir) {
            continue;
        }

        /*
         * During a rename operation, the old name gets
         * IN_MOVED_FROM and the new name gets IN_MOVED_TO.
         * To simplify life for callers, we turn these into
         * DELETED and CREATED events
         */
        switch (iev) {
        case IN_CREATE:
        case IN_MOVED_TO:
            qev = QFILE_MONITOR_EVENT_CREATED;
            break;
        case IN_MODIFY:
            qev = QFILE_MONITOR_EVENT_MODIFIED;
            break;
        case IN_DELETE:
        case IN_MOVED_FROM:
            qev = QFILE_MONITOR_EVENT_DELETED;
            break;
        case IN_IGNORED:
            qev = QFILE_MONITOR_EVENT_IGNORED;
            break;
        default:
            g_assert_not_reached();
        }

        trace_qemu_file_monitor_event(mon, dir->path, name, ev->mask, dir->id);
        for (i = 0; i < dir->nwatches; i++) {
            QFileMonitorWatch *watch = &dir->watches[i];

            if (watch->filename == NULL ||
                (name && g_str_equal(watch->filename, name))) {
                trace_qemu_file_monitor_dispatch(mon, dir->path, name,
                                                 qev, watch->cb,
                                                 watch->opaque, watch->id);
                watch->cb(watch->id, qev, name, watch->opaque);
            }
        }
    }

 cleanup:
    qemu_mutex_unlock(&mon->lock);
}

static void
qemu_file_monitor_dir_free(void *data)
{
    QFileMonitorDir *dir = data;

    g_free(dir->watches);
    g_free(dir);
}

#endif

static QFileMonitor *
qemu_file_monitor_new(Error **errp)
{
#ifdef CONFIG_INOTIFY1
    int fd;
    QFileMonitor *mon;

    fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "Unable to initialize inotify");
        return NULL;
    }

    mon = g_new0(QFileMonitor, 1);
    qemu_mutex_init(&mon->lock);
    mon->fd = fd;

    mon->dirs = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                      qemu_file_monitor_dir_free);
    mon->idmap = g_hash_table_new(g_direct_hash, g_direct_equal);

    trace_qemu_file_monitor_new(mon, mon->fd);

    return mon;
#else
    error_setg(errp, "File monitoring not available on this platform");
    return NULL;
#endif
}


QFileMonitor *qemu_file_monitor_get_instance(Error **errp)
{
    static QFileMonitor *global;

    if (!global) {
        global = qemu_file_monitor_new(errp);
    }

    return global;
}


#ifdef CONFIG_INOTIFY1
int
qemu_file_monitor_add_watch(QFileMonitor *mon,
                            const char *dirpath,
                            const char *filename,
                            QFileMonitorHandler cb,
                            void *opaque,
                            Error **errp)
{
    QFileMonitorDir *dir;
    int ret = -1;

    qemu_mutex_lock(&mon->lock);
    dir = g_hash_table_lookup(mon->dirs, dirpath);
    if (!dir) {
        int rv = inotify_add_watch(mon->fd, dirpath,
                                   IN_CREATE | IN_DELETE | IN_MODIFY |
                                   IN_MOVED_TO | IN_MOVED_FROM);

        if (rv < 0) {
            error_setg_errno(errp, errno, "Unable to watch '%s'", dirpath);
            goto cleanup;
        }

        trace_qemu_file_monitor_enable_watch(mon, dirpath, rv);

        dir = g_new0(QFileMonitorDir, 1);
        dir->path = g_strdup(dirpath);
        dir->id = rv;

        g_hash_table_insert(mon->dirs, dir->path, dir);
        g_hash_table_insert(mon->idmap, GINT_TO_POINTER(rv), dir);

        if (g_hash_table_size(mon->dirs) == 1) {
            qemu_set_fd_handler(mon->fd, qemu_file_monitor_watch, NULL, mon);
        }
    }

    dir->watches = g_renew(QFileMonitorWatch, dir->watches, dir->nwatches + 1);

    dir->watches[dir->nwatches].id = ++dir->nextid;
    dir->watches[dir->nwatches].filename = filename ? g_strdup(filename) : NULL;
    dir->watches[dir->nwatches].cb = cb;
    dir->watches[dir->nwatches].opaque = opaque;
    dir->nwatches++;

    trace_qemu_file_monitor_add_watch(mon, dirpath,
                                      filename ? filename : "<none>",
                                      cb, opaque,
                                      dir->watches[dir->nwatches - 1].id);

    ret = 0;

 cleanup:
    qemu_mutex_unlock(&mon->lock);
    return ret;
}


void qemu_file_monitor_remove_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    int id)
{
    QFileMonitorDir *dir;
    gsize i;

    qemu_mutex_lock(&mon->lock);

    trace_qemu_file_monitor_remove_watch(mon, dirpath, id);

    dir = g_hash_table_lookup(mon->dirs, dirpath);
    if (!dir) {
        goto cleanup;
    }

    for (i = 0; i < dir->nwatches; i++) {
        if (dir->watches[i].id == id) {
            if (i < (dir->nwatches - 1)) {
                memmove(dir->watches + i,
                        dir->watches + i + 1,
                        sizeof(QFileMonitorWatch) *
                        (dir->nwatches - (i + 1)));
                dir->watches = g_renew(QFileMonitorWatch, dir->watches,
                                       dir->nwatches - 1);
                dir->nwatches--;
            }
            break;
        }
    }

    if (dir->nwatches == 0) {
        inotify_rm_watch(mon->fd, dir->id);
        trace_qemu_file_monitor_disable_watch(mon, dir->path, dir->id);

        g_hash_table_remove(mon->idmap, GINT_TO_POINTER(dir->id));
        g_hash_table_remove(mon->dirs, dir->path);
    }

 cleanup:
    qemu_mutex_lock(&mon->lock);
}

#else
int
qemu_file_monitor_add_watch(QFileMonitor *mon,
                            const char *dirpath,
                            const char *filename,
                            QFileMonitorHandler cb,
                            void *opaque,
                            Error **errp)
{
    error_setg(errp, "File monitoring not available on this platform");
    return -1;
}

void qemu_file_monitor_remove_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    int id)
{
}
#endif

