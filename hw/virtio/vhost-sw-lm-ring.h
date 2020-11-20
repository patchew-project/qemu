/*
 * vhost software live migration ring
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2020
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SW_LM_RING_H
#define VHOST_SW_LM_RING_H

#include "qemu/osdep.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

bool vhost_vring_kick(VhostShadowVirtqueue *vq);
int vhost_vring_add(VhostShadowVirtqueue *vq, VirtQueueElement *elem);

/* Called within rcu_read_lock().  */
void vhost_vring_set_notification_rcu(VhostShadowVirtqueue *vq, bool enable);

void vhost_vring_write_addr(const VhostShadowVirtqueue *vq,
	                    struct vhost_vring_addr *addr);

VhostShadowVirtqueue *vhost_sw_lm_shadow_vq(struct vhost_dev *dev, int idx);

void vhost_sw_lm_shadow_vq_free(VhostShadowVirtqueue *vq);

#endif
