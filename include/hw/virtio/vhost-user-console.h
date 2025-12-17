/*
 * Vhost-user console virtio device
 *
 * Copyright (c) 2024-2025 Timos Ampelikiotis <t.ampelikiotis@virtualopensystems.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_CONSOLE_H
#define QEMU_VHOST_USER_CONSOLE_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-user-base.h"

#define TYPE_VHOST_USER_CONSOLE "vhost-user-console"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserConsole, VHOST_USER_CONSOLE)

struct VHostUserConsole {
    VHostUserBase parent;
};

#endif /* QEMU_VHOST_USER_CONSOLE_H */
