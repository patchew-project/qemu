#ifndef QEMU_MIGRATION_FILE_H
#define QEMU_MIGRATION_FILE_H

#include "io/task.h"
#include "channel.h"

void file_start_outgoing_migration(MigrationState *s,
                                   const char *filename,
                                   Error **errp);

void file_start_incoming_migration(const char *fname, Error **errp);
void file_send_channel_create(QIOTaskFunc f, void *data);
int file_send_channel_destroy(QIOChannel *ioc);
#endif
