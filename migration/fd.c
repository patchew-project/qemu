/*
 * QEMU live migration via generic fd
 *
 * Copyright Red Hat, Inc. 2009-2016
 *
 * Authors:
 *  Chris Lalancette <clalance@redhat.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "migration/migration.h"
#include "monitor/monitor.h"
#include "io/channel-util.h"
#include "trace.h"


static void fd_start_outgoing_migration_core(MigrationState *s, int fd,
                                             Error **errp)
{
    QIOChannel *ioc;

    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    migration_channel_connect(s, ioc, NULL);
    object_unref(OBJECT(ioc));
}

void fd_start_outgoing_migration(MigrationState *s, const char *fdname, Error **errp)
{
    int fd = monitor_get_fd(cur_mon, fdname, errp);
    if (fd == -1) {
        return;
    }

    trace_migration_fd_outgoing(fd);
    fd_start_outgoing_migration_core(s, fd, errp);
}

void file_start_outgoing_migration(MigrationState *s, const char *filename,
                                   Error **errp)
{
    int fd;

    fd = qemu_open(filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to open file: %s", filename);
        return;
    }
    
    trace_migration_file_outgoing(filename);
    fd_start_outgoing_migration_core(s, fd, errp);
}

static gboolean fd_accept_incoming_migration(QIOChannel *ioc,
                                             GIOCondition condition,
                                             gpointer opaque)
{
    migration_channel_process_incoming(migrate_get_current(), ioc);
    object_unref(OBJECT(ioc));
    return FALSE; /* unregister */
}

void fd_start_incoming_migration(const char *infd, Error **errp)
{
    QIOChannel *ioc;
    int fd;

    fd = strtol(infd, NULL, 0);
    trace_migration_fd_incoming(fd);

    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    qio_channel_add_watch(ioc,
                          G_IO_IN,
                          fd_accept_incoming_migration,
                          NULL,
                          NULL);
}
