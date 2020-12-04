/*
 * QEMU Securable Guest Memory interface
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

#include "exec/securable-guest-memory.h"

static const TypeInfo securable_guest_memory_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_SECURABLE_GUEST_MEMORY,
    .class_size = sizeof(SecurableGuestMemoryClass),
    .instance_size = sizeof(SecurableGuestMemory),
};

static void securable_guest_memory_register_types(void)
{
    type_register_static(&securable_guest_memory_info);
}

type_init(securable_guest_memory_register_types)
