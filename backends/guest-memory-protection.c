#/*
 * QEMU Guest Memory Protection interface
 *
 * Copyright: David Gibson, Red Hat Inc. 2020
 *
 * Authors:
 *  David Gibson <david@gibson.dropbear.id.au>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "exec/guest-memory-protection.h"

static const TypeInfo guest_memory_protection_info = {
    .name = TYPE_GUEST_MEMORY_PROTECTION,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(GuestMemoryProtectionClass),
};

static void guest_memory_protection_register_types(void)
{
    type_register_static(&guest_memory_protection_info);
}

type_init(guest_memory_protection_register_types)
