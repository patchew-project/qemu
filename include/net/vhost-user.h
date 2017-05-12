/*
 * vhost-user.h
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef NET_VHOST_USER_H
#define NET_VHOST_USER_H

#include "sysemu/char.h"

struct vhost_net;
struct vhost_net *vhost_user_get_vhost_net(NetClientState *nc);
uint64_t vhost_user_get_acked_features(NetClientState *nc);

CharBackend *net_name_to_chr_be(const char *name);

#endif /* VHOST_USER_H */
