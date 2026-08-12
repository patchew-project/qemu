// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPR exec migration via memfd
 *
 * Copyright (c) 2026 Oracle and/or its affiliates.
 */

#ifndef QEMU_MIGRATION_CPR_EXEC_MEMFD_H
#define QEMU_MIGRATION_CPR_EXEC_MEMFD_H

#include "io/channel-file.h"

QIOChannel *cpr_exec_memfd_connect_outgoing(Error **errp);
void cpr_exec_memfd_connect_incoming(Error **errp);
bool cpr_exec_memfd_preserve_fd(Error **errp);
void cpr_exec_memfd_cleanup(void);

#endif
