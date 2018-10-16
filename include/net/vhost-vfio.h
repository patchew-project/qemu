/*
 * vhost-vfio.h
 *
 * Copyright(c) 2017-2018 Intel Corporation. All rights reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VHOST_VFIO_H
#define VHOST_VFIO_H

struct vhost_net;
struct vhost_net *vhost_vfio_get_vhost_net(NetClientState *nc);

#endif /* VHOST_VFIO_H */
