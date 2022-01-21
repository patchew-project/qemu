/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SHADOW_VIRTQUEUE_H
#define VHOST_SHADOW_VIRTQUEUE_H

#include "hw/virtio/vhost.h"

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

VhostShadowVirtqueue *vhost_svq_new(void);

void vhost_svq_free(VhostShadowVirtqueue *vq);

#endif
