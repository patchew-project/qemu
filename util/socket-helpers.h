/*
 * Common helper functions for unix and qemu sockets
 *
 * (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _SOCKET_HELPERS_H_
#define _SOCKET_HELPERS_H_

#include "qapi/qapi-visit-sockets.h"

int unix_connect_saddr(UnixSocketAddress *saddr, Error **errp);
int unix_listen_saddr(UnixSocketAddress *saddr, int num, Error **errp);

#endif /* _SOCKET_HELPERS_H_ */
