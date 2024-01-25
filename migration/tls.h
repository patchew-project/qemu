/*
 * QEMU migration TLS support
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef QEMU_MIGRATION_TLS_H
#define QEMU_MIGRATION_TLS_H

#include "io/channel.h"
#include "io/channel-tls.h"

void migration_tls_channel_process_incoming(MigrationState *s,
                                            QIOChannel *ioc,
                                            Error **errp);

typedef void (*MigTLSConCallback)(QIOChannel *ioc, void *opaque, Error *err);

/**
 * migration_tls_channel_connect:
 * @ioc: The underlying channel object
 * @name: The name of the channel
 * @hostname: The user specified server hostname
 * @callback: The callback to invoke when completed
 * @opaque: Opaque data to pass to @callback
 * @run_in_thread: Whether to run TLS handshake in new thread or not
 * @errp: Pointer to a NULL-initialized error object pointer
 *
 * Establishes a TLS connection on top of the provided QIOChannel @ioc. If this
 * function succeeds, @callback will be invoked upon completion and
 * success/failure will be reported to it via the Error object argument.
 * In case multiple channels are TLS upgraded in parallel, @run_in_thread
 * should be set to true so the TLS handshake will be performed in a new
 * thread, to avoid a potential risk of migration hang.
 *
 * Returns: True on successful initiation of TLS upgrade process, or false on
 * failure.
 */
bool migration_tls_channel_connect(QIOChannel *ioc, const char *name,
                                   const char *hostname,
                                   MigTLSConCallback callback, void *opaque,
                                   bool run_in_thread, Error **errp);

/* Whether the QIO channel requires further TLS handshake? */
bool migrate_channel_requires_tls_upgrade(QIOChannel *ioc);

#endif
