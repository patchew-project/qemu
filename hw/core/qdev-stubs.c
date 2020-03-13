/*
 * QAPI qdev stubs
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qapi-events-qdev.h"

void qapi_event_send_device_deleted(bool has_device,
                                    const char *device, const char *path)
{
}
