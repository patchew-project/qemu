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
#include "qapi/qapi-commands-misc.h"
#include "channel.h"
#include "file.h"
#include "migration.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "monitor/monitor.h"
#include "options.h"
#include "trace.h"

#define OFFSET_OPTION ",offset="

static struct FileOutgoingArgs {
    char *fname;
    int64_t fdset_id;
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

/*
 * If the open flags and file status flags from the file descriptors
 * in the fdset don't match what QEMU expects, errno gets set to
 * EACCES. Let's provide a more user-friendly message.
 */
static void file_fdset_error(int flags, Error **errp)
{
    ERRP_GUARD();

    if (errno == EACCES) {
        /* ditch the previous error */
        error_free(*errp);
        *errp = NULL;

        error_setg(errp, "Fdset is missing a file descriptor with flags: 0x%x",
                   flags);
    }
}

static void file_remove_fdset(void)
{
    if (outgoing_args.fdset_id != -1) {
        qmp_remove_fd(outgoing_args.fdset_id, false, -1, NULL);
        outgoing_args.fdset_id = -1;
    }
}

/*
 * Due to the behavior of the dup() system call, we need the fdset to
 * have two non-duplicate fds so we can enable direct IO in the
 * secondary channels without affecting the main channel.
 */
static bool file_parse_fdset(const char *filename, int64_t *fdset_id,
                             Error **errp)
{
    FdsetInfoList *fds_info;
    FdsetFdInfoList *fd_info;
    const char *fdset_id_str;
    int nfds = 0;

    *fdset_id = -1;

    if (!strstart(filename, "/dev/fdset/", &fdset_id_str)) {
        return true;
    }

    if (!migrate_multifd()) {
        error_setg(errp, "fdset is only supported with multifd");
        return false;
    }

    *fdset_id = qemu_parse_fd(fdset_id_str);

    for (fds_info = qmp_query_fdsets(NULL); fds_info;
         fds_info = fds_info->next) {

        if (*fdset_id != fds_info->value->fdset_id) {
            continue;
        }

        for (fd_info = fds_info->value->fds; fd_info; fd_info = fd_info->next) {
            if (nfds++ > 2) {
                break;
            }
        }
    }

    if (nfds != 2) {
        error_setg(errp, "Outgoing migration needs two fds in the fdset, "
                   "got %d", nfds);
        qmp_remove_fd(*fdset_id, false, -1, NULL);
        *fdset_id = -1;
        return false;
    }

    return true;
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

    file_remove_fdset();
    return 0;
}

void file_send_channel_create(QIOTaskFunc f, void *data)
{
    QIOChannelFile *ioc = NULL;
    QIOTask *task;
    Error *err = NULL;
    int flags = O_WRONLY;

    if (migrate_direct_io()) {
#ifdef O_DIRECT
        /*
         * Enable O_DIRECT for the secondary channels. These are used
         * for sending ram pages and writes should be guaranteed to be
         * aligned to at least page size.
         */
        flags |= O_DIRECT;
#else
        error_setg(&err, "System does not support O_DIRECT");
        error_append_hint(&err,
                          "Try disabling direct-io migration capability\n");
        /* errors are propagated through the qio_task below */
#endif
    }

    if (!err) {
        ioc = qio_channel_file_new_path(outgoing_args.fname, flags, 0, &err);
    }

    task = qio_task_new(OBJECT(ioc), f, (gpointer)data, NULL);
    if (!ioc) {
        file_fdset_error(flags, &err);
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

    if (!file_parse_fdset(filename, &outgoing_args.fdset_id, errp)) {
        return;
    }

    outgoing_args.fname = g_strdup(filename);

    fioc = qio_channel_file_new_path(filename, flags, mode, errp);
    if (!fioc) {
        file_fdset_error(flags, errp);
        return;
    }

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
    int i = 0, fd, flags = O_RDONLY;

    trace_migration_file_incoming(filename);

    fioc = qio_channel_file_new_path(filename, flags, 0, errp);
    if (!fioc) {
        file_fdset_error(flags, errp);
        return;
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

    if (!fioc) {
        error_setg(errp, "Error creating migration incoming channel");
        return;
    }
}
