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
#include "net/vhost_net.h"

typedef struct VhostUserState {
    NetClientState nc;
    CharBackend chr; /* only queue index 0 */
    VHostNetState *vhost_net;
    guint watch;
    uint64_t acked_features;
    bool started;
    /* Pointer to the master device */
    VirtIODevice *vdev;
} VhostUserState;

struct vhost_net;
struct vhost_net *vhost_user_get_vhost_net(NetClientState *nc);
uint64_t vhost_user_get_acked_features(NetClientState *nc);

CharBackend *net_name_to_chr_be(const char *name);

void vhost_user_set_master_dev(NetClientState *nc, VirtIODevice *vdev);

#endif /* VHOST_USER_H */
