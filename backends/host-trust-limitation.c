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

#include "qemu/osdep.h"

#include "exec/host-trust-limitation.h"

static const TypeInfo host_trust_limitation_info = {
    .name = TYPE_HOST_TRUST_LIMITATION,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(HostTrustLimitationClass),
};

static void host_trust_limitation_register_types(void)
{
    type_register_static(&host_trust_limitation_info);
}

type_init(host_trust_limitation_register_types)
