/*
 * vhost software live migration ring
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SHADOW_VIRTQUEUE_H
#define VHOST_SHADOW_VIRTQUEUE_H

#include "qemu/osdep.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

bool vhost_shadow_vq_start_rcu(struct vhost_dev *dev,
                               VhostShadowVirtqueue *svq);
void vhost_shadow_vq_stop_rcu(struct vhost_dev *dev,
                              VhostShadowVirtqueue *svq);

VhostShadowVirtqueue *vhost_shadow_vq_new(struct vhost_dev *dev, int idx);

void vhost_shadow_vq_free(VhostShadowVirtqueue *vq);

#endif
