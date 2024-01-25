/*
 * QEMU live migration channel operations
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_MIGRATION_CHANNEL_H
#define QEMU_MIGRATION_CHANNEL_H

#include "io/channel.h"

void migration_channel_process_incoming(QIOChannel *ioc);

void migration_channel_connect_main(MigrationState *s, QIOChannel *ioc,
                                    const char *hostname, Error *error_in);

int migration_channel_read_peek(QIOChannel *ioc,
                                const char *buf,
                                const size_t buflen,
                                Error **errp);

typedef void (*MigChannelCallback)(QIOChannel *ioc, void *opaque, Error *err);

/**
 * migration_channel_connect:
 * @callback: The callback to invoke when completed
 * @name: The name of the channel
 * @opaque: Opaque data to pass to @callback
 * @tls_in_thread: Whether to run TLS handshake in new thread or not (if TLS is
 *                 needed).
 * @errp: Pointer to a NULL-initialized error object pointer
 *
 * Establishes a new migration channel and TLS upgrades it if needed. If this
 * function succeeds, @callback will be invoked upon completion and
 * success/failure will be reported to it via the Error object.
 * In case multiple channels are established in parallel, @tls_in_thread should
 * be set to true so the TLS handshake will be performed in a new thread, to
 * avoid a potential risk of migration hang.
 *
 * Returns: True on successful initiation of channel establishment process, or
 * false on failure.
 */
bool migration_channel_connect(MigChannelCallback callback, const char *name,
                               void *opaque, bool tls_in_thread, Error **errp);
#endif
