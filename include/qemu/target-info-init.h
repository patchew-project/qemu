/*
 * QEMU target info initialization
 *
 * Copyright (c) Qualcomm
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is included by each file defining a TargetInfo structure and is
 * responsible for registering it.
 */

#ifndef QEMU_TARGET_INFO_INIT_H
#define QEMU_TARGET_INFO_INIT_H

#define DEFINE_TARGET_INFO_TYPE(info)                                       \
static void do_qemu_init_target_info(void)                                  \
{                                                                           \
    type_register_static(&info);                                            \
}                                                                           \
module_init(do_qemu_init_target_info, MODULE_INIT_TARGET_INFO)

#ifdef COMPILING_PER_TARGET
#ifdef CONFIG_USER_ONLY

/*
 * User mode does not support multiple targets in the same binary, so just
 * define target_info().
 */
#define target_info_init(ti_var)        \
const TargetInfo *target_info(void)     \
{                                       \
    return &ti_var;                     \
}

#else /* CONFIG_USER_ONLY */

#include "qemu/target-info-qom.h"
#include "qom/object.h"

/*
 * QOM type names are target-info-<TARGET_NAME>-le and -be so both
 * endian variants can be registered from one system translation unit.
 */
#define TARGET_INFO_QOM_SUFFIX_0 "-le"
#define TARGET_INFO_QOM_SUFFIX_1 "-be"

#define TARGET_INFO_QOM_NAME(sel)                                           \
    TYPE_TARGET_INFO "-" TARGET_NAME glue(TARGET_INFO_QOM_SUFFIX_, sel)

#define TARGET_INFO_QOM_TYPEINFO(ti_var, sel)                               \
static const TypeInfo glue(target_info_qom_type_, sel) = {                  \
    .name = TARGET_INFO_QOM_NAME(sel),                                      \
    .parent = TYPE_TARGET_INFO,                                             \
    .instance_size = sizeof(TargetInfoQom),                                 \
    .class_size = sizeof(TargetInfoQomClass),                               \
    .class_data = &ti_var,                                                  \
}

#define target_info_init_le_be(ti_le, ti_be)                                \
TARGET_INFO_QOM_TYPEINFO(ti_le, 0);                                         \
TARGET_INFO_QOM_TYPEINFO(ti_be, 1);                                         \
static void do_qemu_init_target_info(void)                                  \
{                                                                           \
    type_register_static(&target_info_qom_type_0);                          \
    type_register_static(&target_info_qom_type_1);                          \
}                                                                           \
module_init(do_qemu_init_target_info, MODULE_INIT_TARGET_INFO)

#endif /* CONFIG_USER_ONLY */
#endif /* COMPILING_PER_TARGET */

#endif /* QEMU_TARGET_INFO_INIT_H */
