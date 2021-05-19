/*
 * QEMU Floppy disk emulator (Intel 82078) stubs
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/block/fdc.h"
#include "hw/isa/isa.h"

void isa_fdc_init_drives(ISADevice *fdc, DriveInfo **fds)
{
    g_assert_not_reached();
}
