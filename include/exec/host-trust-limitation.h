/*
 * QEMU Host Trust Limitation interface
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
#ifndef QEMU_HOST_TRUST_LIMITATION_H
#define QEMU_HOST_TRUST_LIMITATION_H

#include "qom/object.h"
#include "hw/boards.h"

#define TYPE_HOST_TRUST_LIMITATION "host-trust-limitation"
#define HOST_TRUST_LIMITATION(obj)                                    \
    INTERFACE_CHECK(HostTrustLimitation, (obj),                       \
                    TYPE_HOST_TRUST_LIMITATION)
#define HOST_TRUST_LIMITATION_CLASS(klass)                            \
    OBJECT_CLASS_CHECK(HostTrustLimitationClass, (klass),             \
                       TYPE_HOST_TRUST_LIMITATION)
#define HOST_TRUST_LIMITATION_GET_CLASS(obj)                          \
    OBJECT_GET_CLASS(HostTrustLimitationClass, (obj),                 \
                     TYPE_HOST_TRUST_LIMITATION)

typedef struct HostTrustLimitationClass {
    InterfaceClass parent;

    int (*kvm_init)(HostTrustLimitation *);
    int (*encrypt_data)(HostTrustLimitation *, uint8_t *, uint64_t);
} HostTrustLimitationClass;

/**
 * host_trust_limitation_enabled - return whether guest memory is protected
 *                                 from hypervisor access (with memory
 *                                 encryption or otherwise)
 * Returns: true guest memory is not directly accessible to qemu
 *          false guest memory is directly accessible to qemu
 */
static inline bool host_trust_limitation_enabled(MachineState *machine)
{
    return !!machine->htl;
}

/**
 * host_trust_limitation_encrypt: encrypt the memory range to make
 *                                it guest accessible
 *
 * Return: 1 failed to encrypt the range
 *         0 succesfully encrypted memory region
 */
static inline int host_trust_limitation_encrypt(MachineState *machine,
                                                uint8_t *ptr, uint64_t len)
{
    HostTrustLimitation *htl = machine->htl;

    if (htl) {
        HostTrustLimitationClass *htlc = HOST_TRUST_LIMITATION_GET_CLASS(htl);

        if (htlc->encrypt_data) {
            return htlc->encrypt_data(htl, ptr, len);
        }
    }

    return 1;
}

#endif /* QEMU_HOST_TRUST_LIMITATION_H */
