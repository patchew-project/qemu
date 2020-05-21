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
#ifndef QEMU_GUEST_MEMORY_PROTECTION_H
#define QEMU_GUEST_MEMORY_PROTECTION_H

#include "qom/object.h"

typedef struct GuestMemoryProtection GuestMemoryProtection;

#define TYPE_GUEST_MEMORY_PROTECTION "guest-memory-protection"
#define GUEST_MEMORY_PROTECTION(obj)                                    \
    INTERFACE_CHECK(GuestMemoryProtection, (obj),                       \
                    TYPE_GUEST_MEMORY_PROTECTION)
#define GUEST_MEMORY_PROTECTION_CLASS(klass)                            \
    OBJECT_CLASS_CHECK(GuestMemoryProtectionClass, (klass),             \
                       TYPE_GUEST_MEMORY_PROTECTION)
#define GUEST_MEMORY_PROTECTION_GET_CLASS(obj)                          \
    OBJECT_GET_CLASS(GuestMemoryProtectionClass, (obj),                 \
                     TYPE_GUEST_MEMORY_PROTECTION)

typedef struct GuestMemoryProtectionClass {
    InterfaceClass parent;

    int (*kvm_init)(GuestMemoryProtection *);
    int (*encrypt_data)(GuestMemoryProtection *, uint8_t *, uint64_t);
} GuestMemoryProtectionClass;

#endif /* QEMU_GUEST_MEMORY_PROTECTION_H */

