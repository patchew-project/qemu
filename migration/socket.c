/*
 * QEMU live migration via socket
 *
 * Copyright Red Hat, Inc. 2009-2016
 *
 * Authors:
 *  Chris Lalancette <clalance@redhat.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "exec/ramblock.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "channel.h"
#include "socket.h"
#include "migration.h"
#include "multifd.h"
#include "qemu-file.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "trace.h"
#include "postcopy-ram.h"
#include "options.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-sockets.h"

struct SocketOutgoingArgs {
    SocketAddress *saddr;
} outgoing_args;

void socket_send_channel_create(QIOTaskFunc f, void *data)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();
    qio_channel_socket_connect_async(sioc, outgoing_args.saddr,
                                     f, data, NULL, NULL);
}

QIOChannel *socket_send_channel_create_sync(Error **errp)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();

    if (!outgoing_args.saddr) {
        object_unref(OBJECT(sioc));
        error_setg(errp, "Initial sock address not set!");
        return NULL;
    }

    if (qio_channel_socket_connect_sync(sioc, outgoing_args.saddr, errp) < 0) {
        object_unref(OBJECT(sioc));
        return NULL;
    }

    return QIO_CHANNEL(sioc);
}

int socket_send_channel_destroy(QIOChannel *send)
{
    /* Remove channel */
    object_unref(OBJECT(send));
    if (outgoing_args.saddr) {
        qapi_free_SocketAddress(outgoing_args.saddr);
        outgoing_args.saddr = NULL;
    }
    return 0;
}

struct SocketConnectData {
    MigrationState *s;
    char *hostname;
};

static void socket_connect_data_free(void *opaque)
{
    struct SocketConnectData *data = opaque;
    if (!data) {
        return;
    }
    g_free(data->hostname);
    g_free(data);
}

static void socket_outgoing_migration(QIOTask *task,
                                      gpointer opaque)
{
    struct SocketConnectData *data = opaque;
    QIOChannel *sioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        trace_migration_socket_outgoing_error(error_get_pretty(err));
           goto out;
    }

    trace_migration_socket_outgoing_connected(data->hostname);

    if (migrate_zero_copy_send() &&
        !qio_channel_has_feature(sioc, QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY)) {
        error_setg(&err, "Zero copy send feature not detected in host kernel");
    }

out:
    migration_channel_connect(data->s, sioc, data->hostname, err);
    object_unref(OBJECT(sioc));
}

void socket_start_outgoing_migration(MigrationState *s,
                                     SocketAddress *saddr,
                                     Error **errp)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();
    struct SocketConnectData *data = g_new0(struct SocketConnectData, 1);
    SocketAddress *addr = QAPI_CLONE(SocketAddress, saddr);

    data->s = s;

    /* in case previous migration leaked it */
    qapi_free_SocketAddress(outgoing_args.saddr);
    outgoing_args.saddr = addr;

    if (saddr->type == SOCKET_ADDRESS_TYPE_INET) {
        data->hostname = g_strdup(saddr->u.inet.host);
    }

    qio_channel_set_name(QIO_CHANNEL(sioc), "migration-socket-outgoing");
    qio_channel_socket_connect_async(sioc,
                                     saddr,
                                     socket_outgoing_migration,
                                     data,
                                     socket_connect_data_free,
                                     NULL);
}

static void socket_accept_incoming_migration(QIONetListener *listener,
                                             QIOChannelSocket *cioc,
                                             gpointer opaque)
{
    trace_migration_socket_incoming_accepted();

    if (migration_has_all_channels()) {
        error_report("%s: Extra incoming migration connection; ignoring",
                     __func__);
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(cioc), "migration-socket-incoming");
    migration_channel_process_incoming(QIO_CHANNEL(cioc));
}

static void
socket_incoming_migration_end(void *opaque)
{
    QIONetListener *listener = opaque;

    qio_net_listener_disconnect(listener);
    object_unref(OBJECT(listener));
}

void socket_start_incoming_migration(SocketAddress *saddr,
                                     Error **errp)
{
    QIONetListener *listener = qio_net_listener_new();
    MigrationIncomingState *mis = migration_incoming_get_current();
    size_t i;
    int num = 1;

    qio_net_listener_set_name(listener, "migration-socket-listener");

    if (migrate_multifd()) {
        num = migrate_multifd_channels();
    } else if (migrate_postcopy_preempt()) {
        num = RAM_CHANNEL_MAX;
    }

    if (qio_net_listener_open_sync(listener, saddr, num, errp) < 0) {
        object_unref(OBJECT(listener));
        return;
    }

    mis->transport_data = listener;
    mis->transport_cleanup = socket_incoming_migration_end;

    qio_net_listener_set_client_func_full(listener,
                                          socket_accept_incoming_migration,
                                          NULL, NULL,
                                          g_main_context_get_thread_default());

    for (i = 0; i < listener->nsioc; i++)  {
        SocketAddress *address =
            qio_channel_socket_get_local_address(listener->sioc[i], errp);
        if (!address) {
            return;
        }
        migrate_add_address(address);
        qapi_free_SocketAddress(address);
    }
}

static int multifd_socket_send_setup(MultiFDSendParams *p, Error **errp)
{
    return 0;
}

static void multifd_socket_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    return;
}

static int multifd_socket_send_prepare(MultiFDSendParams *p, Error **errp)
{
    MultiFDPages_t *pages = p->pages;

    for (int i = 0; i < p->normal_num; i++) {
        p->iov[p->iovs_num].iov_base = pages->block->host + p->normal[i];
        p->iov[p->iovs_num].iov_len = p->page_size;
        p->iovs_num++;
    }

    p->next_packet_size = p->normal_num * p->page_size;
    p->flags |= MULTIFD_FLAG_NOCOMP;
    return 0;
}

static int multifd_socket_send(MultiFDSendParams *p, Error **errp)
{
    int ret;

    if (migrate_zero_copy_send()) {
        /* Send header first, without zerocopy */
        ret = qio_channel_write_all(p->c, (void *)p->packet, p->packet_len,
                                    errp);
        if (ret) {
            return ret;
        }
    } else {
        /* Send header using the same writev call */
        p->iov[0].iov_len = p->packet_len;
        p->iov[0].iov_base = p->packet;
    }

    return qio_channel_writev_full_all(p->c, p->iov, p->iovs_num, NULL,
                                       0, p->write_flags, errp);
}

static int multifd_socket_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    return 0;
}

static void multifd_socket_recv_cleanup(MultiFDRecvParams *p)
{
}

static int multifd_socket_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;

    if (flags != MULTIFD_FLAG_NOCOMP) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_NOCOMP);
        return -1;
    }
    for (int i = 0; i < p->normal_num; i++) {
        p->iov[i].iov_base = p->host + p->normal[i];
        p->iov[i].iov_len = p->page_size;
    }
    return qio_channel_readv_all(p->c, p->iov, p->normal_num, errp);
}

MultiFDMethods multifd_socket_ops = {
    .send_setup = multifd_socket_send_setup,
    .send = multifd_socket_send,
    .send_cleanup = multifd_socket_send_cleanup,
    .send_prepare = multifd_socket_send_prepare,
    .recv_setup = multifd_socket_recv_setup,
    .recv_cleanup = multifd_socket_recv_cleanup,
    .recv_pages = multifd_socket_recv_pages
};
