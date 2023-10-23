/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "channel.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "trace.h"

#define OFFSET_OPTION ",offset="

static struct FileOutgoingArgs {
    char *fname;
    int flags;
    int mode;
} outgoing_args;

/* Remove the offset option from @filespec and return it in @offsetp. */

static int file_parse_offset(char *filespec, uint64_t *offsetp, Error **errp)
{
    char *option = strstr(filespec, OFFSET_OPTION);
    int ret;

    if (option) {
        *option = 0;
        option += sizeof(OFFSET_OPTION) - 1;
        ret = qemu_strtosz(option, NULL, offsetp);
        if (ret) {
            error_setg_errno(errp, -ret, "file URI has bad offset %s", option);
            return -1;
        }
    }
    return 0;
}

static void qio_channel_file_connect_worker(QIOTask *task, gpointer opaque)
{
    /* noop */
}

static void file_migration_cancel(Error *errp)
{
    MigrationState *s;

    s = migrate_get_current();

    migrate_set_state(&s->state, MIGRATION_STATUS_SETUP,
                      MIGRATION_STATUS_FAILED);
    migration_cancel(errp);
}

int file_send_channel_destroy(QIOChannel *ioc)
{
    if (ioc) {
        qio_channel_close(ioc, NULL);
        object_unref(OBJECT(ioc));
    }
    g_free(outgoing_args.fname);
    outgoing_args.fname = NULL;

    return 0;
}

void file_send_channel_create(QIOTaskFunc f, void *data)
{
    QIOChannelFile *ioc;
    QIOTask *task;
    Error *errp = NULL;

    ioc = qio_channel_file_new_path(outgoing_args.fname,
                                    outgoing_args.flags,
                                    outgoing_args.mode, &errp);
    if (!ioc) {
        file_migration_cancel(errp);
        return;
    }

    task = qio_task_new(OBJECT(ioc), f, (gpointer)data, NULL);
    qio_task_run_in_thread(task, qio_channel_file_connect_worker,
                           (gpointer)data, NULL, NULL);
}

void file_start_outgoing_migration(MigrationState *s, const char *filespec,
                                   Error **errp)
{
    g_autoptr(QIOChannelFile) fioc = NULL;
    g_autofree char *filename = g_strdup(filespec);
    uint64_t offset = 0;
    QIOChannel *ioc;
    int flags = O_CREAT | O_TRUNC | O_WRONLY;
    mode_t mode = 0660;

    trace_migration_file_outgoing(filename);

    if (file_parse_offset(filename, &offset, errp)) {
        return;
    }

    fioc = qio_channel_file_new_path(filename, flags, mode, errp);
    if (!fioc) {
        return;
    }

    outgoing_args.fname = g_strdup(filename);
    outgoing_args.flags = flags;
    outgoing_args.mode = mode;

    ioc = QIO_CHANNEL(fioc);
    if (offset && qio_channel_io_seek(ioc, offset, SEEK_SET, errp) < 0) {
        return;
    }
    qio_channel_set_name(ioc, "migration-file-outgoing");
    migration_channel_connect(s, ioc, NULL, NULL);
}

static gboolean file_accept_incoming_migration(QIOChannel *ioc,
                                               GIOCondition condition,
                                               gpointer opaque)
{
    migration_channel_process_incoming(ioc);
    object_unref(OBJECT(ioc));
    return G_SOURCE_REMOVE;
}

void file_start_incoming_migration(const char *filespec, Error **errp)
{
    g_autofree char *filename = g_strdup(filespec);
    QIOChannelFile *fioc = NULL;
    uint64_t offset = 0;
    QIOChannel *ioc;

    trace_migration_file_incoming(filename);

    if (file_parse_offset(filename, &offset, errp)) {
        return;
    }

    fioc = qio_channel_file_new_path(filename, O_RDONLY, 0, errp);
    if (!fioc) {
        return;
    }

    ioc = QIO_CHANNEL(fioc);
    if (offset && qio_channel_io_seek(ioc, offset, SEEK_SET, errp) < 0) {
        return;
    }
    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-file-incoming");
    qio_channel_add_watch_full(ioc, G_IO_IN,
                               file_accept_incoming_migration,
                               NULL, NULL,
                               g_main_context_get_thread_default());
}
