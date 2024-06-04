/*
 * QEMU live migration via RDMA
 *
 * Copyright (c) 2024 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *  Jialin Wang <wangjialin23@huawei.com>
 *  Gonglei <arei.gonglei@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_RDMA_H
#define QEMU_MIGRATION_RDMA_H

#include "qemu/sockets.h"

void rdma_start_outgoing_migration(MigrationState *s, InetSocketAddress *addr,
                                   Error **errp);

void rdma_start_incoming_migration(InetSocketAddress *addr, Error **errp);

#endif /* QEMU_MIGRATION_RDMA_H */
