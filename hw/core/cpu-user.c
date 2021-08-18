/*
 * QEMU CPU model (user-only emulation specific)
 *
 * Copyright (c) 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#include "cpu-common.h"

/*
 * This can't go in hw/core/cpu-common.c because that file is compiled only
 * once for both user-mode and system builds.
 */
static Property cpu_useronly_props[] = {
    /*
     * Create a memory property for softmmu CPU object, so users can wire
     * up its memory. The default if no link is set up is to use the
     * system address space.
     */
#if 0
    DEFINE_PROP_BOOL("prctl-unalign-sigbus", CPUState,
                     prctl_unalign_sigbus, false),
#endif
    DEFINE_PROP_END_OF_LIST(),
};

void cpu_class_init_props(DeviceClass *dc)
{
    device_class_set_props(dc, cpu_useronly_props);
}
