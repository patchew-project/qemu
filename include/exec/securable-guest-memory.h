/*
 * QEMU Securable Guest Memory interface
 *   This interface describes the common pieces between various
 *   schemes for protecting guest memory against a compromised
 *   hypervisor.  This includes memory encryption (AMD's SEV and
 *   Intel's MKTME) or special protection modes (PEF on POWER, or PV
 *   on s390x).
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
#ifndef QEMU_SECURABLE_GUEST_MEMORY_H
#define QEMU_SECURABLE_GUEST_MEMORY_H

#ifndef CONFIG_USER_ONLY

#include "qom/object.h"
#include "hw/boards.h"

#define TYPE_SECURABLE_GUEST_MEMORY "securable-guest-memory"
#define SECURABLE_GUEST_MEMORY(obj)                                    \
    OBJECT_CHECK(SecurableGuestMemory, (obj),                          \
                 TYPE_SECURABLE_GUEST_MEMORY)
#define SECURABLE_GUEST_MEMORY_CLASS(klass)                            \
    OBJECT_CLASS_CHECK(SecurableGuestMemoryClass, (klass),             \
                       TYPE_SECURABLE_GUEST_MEMORY)
#define SECURABLE_GUEST_MEMORY_GET_CLASS(obj)                          \
    OBJECT_GET_CLASS(SecurableGuestMemoryClass, (obj),                 \
                     TYPE_SECURABLE_GUEST_MEMORY)

struct SecurableGuestMemory {
    Object parent;
};

typedef struct SecurableGuestMemoryClass {
    ObjectClass parent;

    int (*encrypt_data)(SecurableGuestMemory *, uint8_t *, uint64_t);
} SecurableGuestMemoryClass;

/**
 * securable_guest_memory_enabled - return whether guest memory is protected
 *                               from hypervisor access (with memory
 *                               encryption or otherwise)
 * Returns: true guest memory is not directly accessible to qemu
 *          false guest memory is directly accessible to qemu
 */
static inline bool securable_guest_memory_enabled(MachineState *machine)
{
    return !!machine->sgm;
}

/**
 * securable_guest_memory_encrypt: encrypt the memory range to make
 *                              it guest accessible
 *
 * Return: 1 failed to encrypt the range
 *         0 succesfully encrypted memory region
 */
static inline int securable_guest_memory_encrypt(MachineState *machine,
                                              uint8_t *ptr, uint64_t len)
{
    SecurableGuestMemory *sgm = machine->sgm;

    if (sgm) {
        SecurableGuestMemoryClass *sgmc = SECURABLE_GUEST_MEMORY_GET_CLASS(sgm);

        if (sgmc->encrypt_data) {
            return sgmc->encrypt_data(sgm, ptr, len);
        }
    }

    return 1;
}

#endif /* !CONFIG_USER_ONLY */

#endif /* QEMU_SECURABLE_GUEST_MEMORY_H */
