/*
 * Virtio QMP helpers
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_VIRTIO_QMP_H
#define HW_VIRTIO_QMP_H

#include "qapi/qapi-types-virtio.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"

VirtIODevice *qmp_find_virtio_device(const char *path);

#endif
