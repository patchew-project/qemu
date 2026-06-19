/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU Guest Agent common GIOChannel lifecycle
 *
 * Copyright (c) 2026 Virtuozzo International GmbH.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/sockets.h"
#include "channel-common.h"

void ga_channel_common_gio_destroy(GIOChannel *ch)
{
    g_io_channel_shutdown(ch, true, NULL);
    g_io_channel_unref(ch);
}

/* The vsock analogue of unix_listen(): returns a listening fd or -1. */
int ga_channel_common_vsock_listen(const char *path, Error **errp)
{
    SocketAddress *addr;
    char *addr_str;
    int fd;

    addr_str = g_strdup_printf("vsock:%s", path);
    addr = socket_parse(addr_str, errp);
    g_free(addr_str);
    if (!addr) {
        return -1;
    }

    fd = socket_listen(addr, 1, errp);
    qapi_free_SocketAddress(addr);
    return fd;
}

void ga_channel_common_listen_close(GAChannelCommon *c)
{
    g_assert(c->listen_channel);
    ga_channel_common_gio_destroy(c->listen_channel);
    c->listen_channel = NULL;
}

int ga_channel_common_client_add(GAChannelCommon *c, GIOChannel *client_ch,
                                 GIOFunc event_fn, gpointer data)
{
    GError *err = NULL;

    g_assert(c && !c->client_channel);
    g_io_channel_set_encoding(client_ch, NULL, &err);
    if (err != NULL) {
        g_warning("error setting channel encoding to binary");
        g_error_free(err);
        return -1;
    }
    g_io_add_watch(client_ch, G_IO_IN | G_IO_HUP, event_fn, data);
    c->client_channel = client_ch;
    return 0;
}

void ga_channel_common_client_close(GAChannelCommon *c)
{
    g_assert(c->client_channel);
    ga_channel_common_gio_destroy(c->client_channel);
    c->client_channel = NULL;
}

GIOStatus ga_channel_common_read(GAChannelCommon *c, gchar *buf,
                                 gsize size, gsize *count)
{
    return g_io_channel_read_chars(c->client_channel, buf, size, count, NULL);
}

GIOStatus ga_channel_common_write_all(GAChannelCommon *c,
                                      const gchar *buf, gsize size)
{
    GError *err = NULL;
    gsize written = 0;
    GIOStatus status = G_IO_STATUS_NORMAL;

    while (size) {
        g_debug("sending data, count: %d", (int)size);
        status = g_io_channel_write_chars(c->client_channel, buf, size,
                                          &written, &err);
        if (status == G_IO_STATUS_NORMAL) {
            size -= written;
            buf += written;
        } else if (status != G_IO_STATUS_AGAIN) {
            g_warning("error writing to channel: %s", err->message);
            return status;
        }
    }

    do {
        status = g_io_channel_flush(c->client_channel, &err);
    } while (status == G_IO_STATUS_AGAIN);

    if (status != G_IO_STATUS_NORMAL) {
        g_warning("error flushing channel: %s", err->message);
    }

    return status;
}

void ga_channel_common_free(GAChannelCommon *c)
{
    if (c->listen_channel) {
        ga_channel_common_listen_close(c);
    }
    if (c->client_channel) {
        ga_channel_common_client_close(c);
    }
}
