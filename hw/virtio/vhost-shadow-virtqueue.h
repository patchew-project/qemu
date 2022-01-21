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
#include "qemu/event_notifier.h"

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

bool vhost_svq_valid_device_features(uint64_t *features);
bool vhost_svq_valid_guest_features(uint64_t *features);
bool vhost_svq_ack_guest_features(uint64_t dev_features,
                                  uint64_t guest_features,
                                  uint64_t *acked_features);

void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd);
void vhost_svq_set_guest_call_notifier(VhostShadowVirtqueue *svq, int call_fd);
const EventNotifier *vhost_svq_get_dev_kick_notifier(
                                              const VhostShadowVirtqueue *svq);
const EventNotifier *vhost_svq_get_svq_call_notifier(
                                              const VhostShadowVirtqueue *svq);
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr);
uint16_t vhost_svq_get_num(const VhostShadowVirtqueue *svq);
size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq);
size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq);

void vhost_svq_stop(VhostShadowVirtqueue *svq);

VhostShadowVirtqueue *vhost_svq_new(uint16_t qsize);

void vhost_svq_free(VhostShadowVirtqueue *vq);

#endif
