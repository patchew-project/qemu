/*
 * QEMU Confidential Guest support
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

#include "exec/confidential-guest-support.h"

static const TypeInfo confidential_guest_support_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_CONFIDENTIAL_GUEST_SUPPORT,
    .class_size = sizeof(ConfidentialGuestSupportClass),
    .instance_size = sizeof(ConfidentialGuestSupport),
};

static void confidential_guest_support_register_types(void)
{
    type_register_static(&confidential_guest_support_info);
}

type_init(confidential_guest_support_register_types)
