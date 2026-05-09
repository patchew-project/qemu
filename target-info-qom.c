/*
 * QEMU binary/target API (QOM types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "qemu/target-info-qom.h"
#include "hw/arm/machines-qom.h"

static const TypeInfo target_info_types[] = {
    {
        .name           = TYPE_TARGET_ARM_MACHINE,
        .parent         = TYPE_INTERFACE,
    },
    {
        .name           = TYPE_TARGET_AARCH64_MACHINE,
        .parent         = TYPE_INTERFACE,
    },
};

DEFINE_TYPES(target_info_types)

static const TypeInfo target_info_parent_type = {
    .name = TYPE_TARGET_INFO,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(TargetInfoQom),
    .class_size = sizeof(TargetInfoQomClass),
    .abstract = true,
};

DEFINE_TARGET_INFO_TYPE(target_info_parent_type)
