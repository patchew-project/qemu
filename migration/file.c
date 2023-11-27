/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "channel.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "options.h"
#include "trace.h"

#define OFFSET_OPTION ",offset="

static struct FileOutgoingArgs {
    char *fname;
} outgoing_args;

/* Remove the offset option from @filespec and return it in @offsetp. */

int file_parse_offset(char *filespec, uint64_t *offsetp, Error **errp)
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
    Error *err = NULL;
    int flags = O_WRONLY;

    ioc = qio_channel_file_new_path(outgoing_args.fname, flags, 0, &err);

    task = qio_task_new(OBJECT(ioc), f, (gpointer)data, NULL);
    if (!ioc) {
        qio_task_set_error(task, err);
        return;
    }

    qio_task_run_in_thread(task, qio_channel_file_connect_worker,
                           (gpointer)data, NULL, NULL);
}

void file_start_outgoing_migration(MigrationState *s,
                                   FileMigrationArgs *file_args, Error **errp)
{
    g_autoptr(QIOChannelFile) fioc = NULL;
    g_autofree char *filename = g_strdup(file_args->filename);
    uint64_t offset = file_args->offset;
    QIOChannel *ioc;
    int flags = O_CREAT | O_TRUNC | O_WRONLY;
    mode_t mode = 0660;

    trace_migration_file_outgoing(filename);

    fioc = qio_channel_file_new_path(filename, flags, mode, errp);
    if (!fioc) {
        return;
    }

    outgoing_args.fname = g_strdup(filename);

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

void file_start_incoming_migration(FileMigrationArgs *file_args, Error **errp)
{
    g_autofree char *filename = g_strdup(file_args->filename);
    QIOChannelFile *fioc = NULL;
    uint64_t offset = file_args->offset;
    int channels = 1;
    int i = 0, fd;

    trace_migration_file_incoming(filename);

    fioc = qio_channel_file_new_path(filename, O_RDONLY, 0, errp);
    if (!fioc) {
        goto out;
    }

    if (offset &&
        qio_channel_io_seek(QIO_CHANNEL(fioc), offset, SEEK_SET, errp) < 0) {
        return;
    }

    if (migrate_multifd()) {
        channels += migrate_multifd_channels();
    }

    fd = fioc->fd;

    do {
        QIOChannel *ioc = QIO_CHANNEL(fioc);

        qio_channel_set_name(ioc, "migration-file-incoming");
        qio_channel_add_watch_full(ioc, G_IO_IN,
                                   file_accept_incoming_migration,
                                   NULL, NULL,
                                   g_main_context_get_thread_default());
    } while (++i < channels && (fioc = qio_channel_file_new_fd(fd)));

out:
    if (!fioc) {
        error_setg(errp, "Error creating migration incoming channel");
        return;
    }
}
