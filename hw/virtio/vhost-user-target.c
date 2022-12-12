/*
 * vhost-user target-specific helpers
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/vhost-user.h"

#if defined(TARGET_X86) || defined(TARGET_X86_64) || \
    defined(TARGET_ARM) || defined(TARGET_ARM_64)
#include "hw/acpi/acpi.h"
#elif defined(TARGET_PPC) || defined(TARGET_PPC64)
#include "hw/ppc/spapr.h"
#endif

unsigned int vhost_user_ram_slots_max(void)
{
#if defined(TARGET_X86) || defined(TARGET_X86_64) || \
    defined(TARGET_ARM) || defined(TARGET_ARM_64)
    return ACPI_MAX_RAM_SLOTS;
#elif defined(TARGET_PPC) || defined(TARGET_PPC64)
    return SPAPR_MAX_RAM_SLOTS;
#else
    return 512;
#endif
}
