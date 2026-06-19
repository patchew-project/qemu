/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU Guest Agent common GIOChannel lifecycle
 *
 * Copyright (c) 2026 Virtuozzo International GmbH.
 */
#ifndef QGA_CHANNEL_COMMON_H
#define QGA_CHANNEL_COMMON_H

#include "channel.h"

typedef struct GAChannelCommon {
    GIOChannel *listen_channel;
    GIOChannel *client_channel;
    GAChannelMethod method;
    GAChannelCallback event_cb;
    gpointer user_data;
} GAChannelCommon;

/* Common GIOChannel lifecycle (channel-common.c) */
void ga_channel_common_listen_close(GAChannelCommon *c);

/* Start listening on a vsock "cid:port" path; returns an fd or -1. */
int ga_channel_common_vsock_listen(const char *path, Error **errp);

/* Shut down and unref a GIOChannel not tracked by a GAChannelCommon. */
void ga_channel_common_gio_destroy(GIOChannel *ch);

/*
 * Takes ownership of client_ch on success only; on failure the caller keeps
 * it and must dispose of it via ga_channel_common_gio_destroy().
 */
int ga_channel_common_client_add(GAChannelCommon *c,
                                 GIOChannel *client_ch,
                                 GIOFunc event_fn, gpointer data);
void ga_channel_common_client_close(GAChannelCommon *c);
GIOStatus ga_channel_common_read(GAChannelCommon *c, gchar *buf,
                                 gsize size, gsize *count);
GIOStatus ga_channel_common_write_all(GAChannelCommon *c,
                                      const gchar *buf, gsize size);
void ga_channel_common_free(GAChannelCommon *c);

#endif /* QGA_CHANNEL_COMMON_H */
