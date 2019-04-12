/*
 * QEMU live migration via generic fd passed with command
 *
 * Copyright Yandex, Inc. 2019
 *
 * Authors:
 *  Yury Kotov <yury-kotov@yandex-team.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_MIGRATION_INLINE_FD_H
#define QEMU_MIGRATION_INLINE_FD_H

void inline_fd_start_incoming_migration(Error **errp);
void inline_fd_start_outgoing_migration(MigrationState *s, Error **errp);

#endif
