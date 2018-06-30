/*
 * ARMv6M CPU object
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * This code is licensed under the GPL version 2 or later.
 */

#include "qemu/osdep.h"
#include "hw/arm/armv6m.h"

static const TypeInfo armv6m_info = {
    .name = TYPE_ARMV6M,
    .parent = TYPE_ARM_M_PROFILE,
    .instance_size = sizeof(ARMv6MState),
};

static void armv6m_register_types(void)
{
    type_register_static(&armv6m_info);
}

type_init(armv6m_register_types)
