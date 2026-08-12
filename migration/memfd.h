// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU live migration via memfd
 *
 * Copyright (c) 2026 Oracle and/or its affiliates.
 */

#ifndef QEMU_MIGRATION_MEMFD_H
#define QEMU_MIGRATION_MEMFD_H

#include "io/channel-file.h"

QIOChannel *memfd_create_outgoing(const char *name, int *dup_fdp,
                                  Error **errp);
QIOChannel *memfd_open_incoming(int fd, const char *name, Error **errp);
QIOChannel *memfd_connect_outgoing(Error **errp);
void memfd_connect_incoming(Error **errp);

#endif
