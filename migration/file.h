/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_FILE_H
#define QEMU_MIGRATION_FILE_H

#include "io/task.h"
#include "channel.h"

void file_start_incoming_migration(const char *filespec, Error **errp);

void file_start_outgoing_migration(MigrationState *s, const char *filespec,
                                   Error **errp);
void file_send_channel_create(QIOTaskFunc f, void *data);
int file_send_channel_destroy(QIOChannel *ioc);
#endif
