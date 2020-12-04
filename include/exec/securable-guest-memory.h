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

#endif /* !CONFIG_USER_ONLY */

#endif /* QEMU_SECURABLE_GUEST_MEMORY_H */
