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
#include "qemu/target-info-qom.h"
#include "hw/arm/machines-qom.h"

static const TypeInfo target_info_types[] = {
    {
        .name = TYPE_TARGET_INFO,
        .parent = TYPE_OBJECT,
        .instance_size = sizeof(TargetInfoQom),
        .class_size = sizeof(TargetInfoQomClass),
        .abstract = true,
    },
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

static const TargetInfo *target_info_ptr;

const TargetInfo *target_info(void)
{
    return target_info_ptr;
}

void target_info_qom_set_target(void)
{
    g_autoptr(GSList) targets =
        object_class_get_list_by_name_prefix(TYPE_TARGET_INFO, false);

    size_t num_found = g_slist_length(targets);
    if (num_found != 1) {
        error_setg(&error_fatal, num_found == 0 ?
                                 "no target-info is available" :
                                 "more than one target-info is available");
    }

    target_info_ptr = TARGET_INFO_CLASS(targets->data)->target_info;
}
