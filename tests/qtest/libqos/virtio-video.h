/*
 * libqos virtio-video definitions
 *
 * Copyright (c) 2023 Red Hat Inc.
 *
 * Authors:
 *  Albert Esteve <aesteve@redhat.com>
 *   (based on virtio-gpio.h)
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TESTS_LIBQOS_VIRTIO_VIDEO_H
#define TESTS_LIBQOS_VIRTIO_VIDEO_H

#include "qgraph.h"
#include "virtio.h"
#include "virtio-pci.h"

typedef struct QVhostUserVideo QVhostUserVideo;
typedef struct QVhostUserVideoPCI QVhostUserVideoPCI;
typedef struct QVhostUserVideoDevice QVhostUserVideoDevice;

struct QVhostUserVideo {
    QVirtioDevice *vdev;
    QVirtQueue **queues;
};

struct QVhostUserVideoPCI {
    QVirtioPCIDevice pci_vdev;
    QVhostUserVideo video;
};

struct QVhostUserVideoDevice {
    QOSGraphObject obj;
    QVhostUserVideo video;
};

#endif /* TESTS_LIBQOS_VIRTIO_VIDEO_H */
