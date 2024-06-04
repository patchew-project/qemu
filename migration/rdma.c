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

#include "qemu/osdep.h"
#include "io/channel-rdma.h"
#include "io/channel.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-types-sockets.h"
#include "qapi/qapi-visit-sockets.h"
#include "channel.h"
#include "migration.h"
#include "rdma.h"
#include "trace.h"
#include <stdio.h>

static struct RDMAOutgoingArgs {
    InetSocketAddress *addr;
} outgoing_args;

static void rdma_outgoing_migration(QIOTask *task, gpointer opaque)
{
    MigrationState *s = opaque;
    QIOChannelRDMA *rioc = QIO_CHANNEL_RDMA(qio_task_get_source(task));

    migration_channel_connect(s, QIO_CHANNEL(rioc), outgoing_args.addr->host,
                              NULL);
    object_unref(OBJECT(rioc));
}

void rdma_start_outgoing_migration(MigrationState *s, InetSocketAddress *iaddr,
                                   Error **errp)
{
    QIOChannelRDMA *rioc = qio_channel_rdma_new();

    /* in case previous migration leaked it */
    qapi_free_InetSocketAddress(outgoing_args.addr);
    outgoing_args.addr = QAPI_CLONE(InetSocketAddress, iaddr);

    qio_channel_set_name(QIO_CHANNEL(rioc), "migration-rdma-outgoing");
    qio_channel_rdma_connect_async(rioc, iaddr, rdma_outgoing_migration, s,
                                   NULL, NULL);
}

static void coroutine_fn rdma_accept_incoming_migration(void *opaque)
{
    QIOChannelRDMA *rioc = opaque;
    QIOChannelRDMA *cioc;

    while (!migration_has_all_channels()) {
        cioc = qio_channel_rdma_accept(rioc, NULL);

        qio_channel_set_name(QIO_CHANNEL(cioc), "migration-rdma-incoming");
        migration_channel_process_incoming(QIO_CHANNEL(cioc));
        object_unref(OBJECT(cioc));
    }
}

void rdma_start_incoming_migration(InetSocketAddress *addr, Error **errp)
{
    QIOChannelRDMA *rioc = qio_channel_rdma_new();
    MigrationIncomingState *mis = migration_incoming_get_current();
    Coroutine *co;
    int num = 1;

    qio_channel_set_name(QIO_CHANNEL(rioc), "migration-rdma-listener");

    if (qio_channel_rdma_listen_sync(rioc, addr, num, errp) < 0) {
        object_unref(OBJECT(rioc));
        return;
    }

    mis->transport_data = rioc;
    mis->transport_cleanup = object_unref;

    qio_channel_set_blocking(QIO_CHANNEL(rioc), false, NULL);
    co = qemu_coroutine_create(rdma_accept_incoming_migration, rioc);
    aio_co_schedule(qemu_get_current_aio_context(), co);
}
