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

#endif /* QEMU_HOST_TRUST_LIMITATION_H */
