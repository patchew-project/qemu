/*
 * Unified Hosting Interface syscalls stubs
 *
 * Copyright (c) 2019 Red Hat, Inc.
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
#include "cpu.h"
#include "exec/helper-proto.h"

void helper_do_semihosting(CPUMIPSState *env)
{
    g_assert_not_reached();
}
