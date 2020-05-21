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
#include "hw/boards.h"

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

    int (*kvm_init)(GuestMemoryProtection *, Error **);
    int (*encrypt_data)(GuestMemoryProtection *, uint8_t *, uint64_t);
} GuestMemoryProtectionClass;

/**
 * guest_memory_protection_enabled - return whether guest memory is
 *                                   protected from hypervisor access
 *                                   (with memory encryption or
 *                                   otherwise)
 * Returns: true guest memory is not directly accessible to qemu
 *          false guest memory is directly accessible to qemu
 */
static inline bool guest_memory_protection_enabled(MachineState *machine)
{
    return !!machine->gmpo;
}

/**
 * guest_memory_protection_encrypt: encrypt the memory range to make
 *                                  it guest accessible
 *
 * Return: 1 failed to encrypt the range
 *         0 succesfully encrypted memory region
 */
static inline int guest_memory_protection_encrypt(MachineState *machine,
                                                  uint8_t *ptr, uint64_t len)
{
    GuestMemoryProtection *gmpo = machine->gmpo;

    if (gmpo) {
        GuestMemoryProtectionClass *gmpc =
            GUEST_MEMORY_PROTECTION_GET_CLASS(gmpo);

        if (gmpc->encrypt_data) {
            return gmpc->encrypt_data(gmpo, ptr, len);
        }
    }

    return 1;
}

#endif /* QEMU_GUEST_MEMORY_PROTECTION_H */

