/*
 * QEMU live migration via generic fd passed with command
 *
 * Copyright Yandex, Inc. 2019
 *
 * Authors:
 *  Yury Kotov <yury-kotov@yandex-team.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "channel.h"
#include "inline-fd.h"
#include "monitor/monitor.h"
#include "io/channel-util.h"
#include "trace.h"


void inline_fd_start_outgoing_migration(MigrationState *s, Error **errp)
{
    QIOChannel *ioc;
    int fd;

    if (!cur_mon) {
        error_setg(errp, "Monitor is disabled");
        return;
    }

    fd = monitor_recv_fd(cur_mon, errp);
    if (fd == -1) {
        return;
    }

    trace_migration_inline_fd_outgoing(fd);
    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-infd-outgoing");
    migration_channel_connect(s, ioc, NULL, NULL);
    object_unref(OBJECT(ioc));
}

static gboolean inline_fd_accept_incoming_migration(QIOChannel *ioc,
                                                    GIOCondition condition,
                                                    gpointer opaque)
{
    migration_channel_process_incoming(ioc);
    object_unref(OBJECT(ioc));
    return G_SOURCE_REMOVE;
}

void inline_fd_start_incoming_migration(Error **errp)
{
    QIOChannel *ioc;
    int fd;

    if (!cur_mon) {
        error_setg(errp, "Monitor is disabled");
        return;
    }

    fd = monitor_recv_fd(cur_mon, errp);
    if (fd == -1) {
        return;
    }

    trace_migration_inline_fd_incoming(fd);
    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-infd-incoming");
    qio_channel_add_watch(ioc,
                          G_IO_IN,
                          inline_fd_accept_incoming_migration,
                          NULL,
                          NULL);
}
