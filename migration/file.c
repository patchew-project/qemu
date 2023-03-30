#include "qemu/osdep.h"
#include "io/channel-file.h"
#include "file.h"
#include "qemu/error-report.h"

static struct FileOutgoingArgs {
    char *fname;
    int flags;
    int mode;
} outgoing_args;

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

void file_start_outgoing_migration(MigrationState *s, const char *fname, Error **errp)
{
    QIOChannelFile *ioc;
    int flags = O_CREAT | O_TRUNC | O_WRONLY;
    mode_t mode = 0660;

    ioc = qio_channel_file_new_path(fname, flags, mode, errp);
    if (!ioc) {
        error_report("Error creating migration outgoing channel");
        return;
    }

    outgoing_args.fname = g_strdup(fname);
    outgoing_args.flags = flags;
    outgoing_args.mode = mode;

    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-file-outgoing");
    migration_channel_connect(s, QIO_CHANNEL(ioc), NULL, NULL);
    object_unref(OBJECT(ioc));
}

void file_start_incoming_migration(const char *fname, Error **errp)
{
    QIOChannelFile *ioc;

    ioc = qio_channel_file_new_path(fname, O_RDONLY, 0, errp);
    if (!ioc) {
        error_report("Error creating a channel");
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-file-incoming");
    migration_channel_process_incoming(QIO_CHANNEL(ioc));
    object_unref(OBJECT(ioc));
}
