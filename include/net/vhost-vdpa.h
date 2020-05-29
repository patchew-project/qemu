/*
 * vhost-vdpa.h
 *
 * Copyright(c) 2017-2018 Intel Corporation.
 * Copyright(c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VHOST_VDPA_H
#define VHOST_VDPA_H

struct vhost_net;
struct vhost_net *vhost_vdpa_get_vhost_net(NetClientState *nc);
uint64_t vhost_vdpa_get_acked_features(NetClientState *nc);

#endif /* VHOST_VDPA_H */
