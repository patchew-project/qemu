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

#ifndef TARGET_INFO_DEF_H
#define TARGET_INFO_DEF_H

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

#define TYPE_TARGET_INFO_TARGET TYPE_TARGET_INFO"-"TARGET_NAME

typedef struct TargetInfoQomTarget {
    TargetInfoQom parent;
} TargetInfoQomTarget;

typedef struct TargetInfoQomTargetClass {
    TargetInfoQomClass parent_class;
} TargetInfoQomTargetClass;

OBJECT_DECLARE_TYPE(TargetInfoQomTarget, TargetInfoQomTargetClass, TARGET_INFO_TARGET)

#define target_info_init(ti_var)                                            \
const TargetInfo *target_info(void)                                         \
{                                                                           \
    return &ti_var;                                                         \
}                                                                           \
                                                                            \
static void target_info_qom_class_init(ObjectClass *oc, const void * data)  \
{                                                                           \
    TargetInfoQomTargetClass *klass = TARGET_INFO_TARGET_CLASS(oc);         \
    klass->parent_class.target_info = &ti_var;                              \
}                                                                           \
                                                                            \
static const TypeInfo target_info_qom_target_type_info[] = {                \
{                                                                           \
    .name = TYPE_TARGET_INFO_TARGET,                                        \
    .parent = TYPE_TARGET_INFO,                                             \
    .instance_size = sizeof(TargetInfoQomTarget),                           \
    .class_size = sizeof(TargetInfoQomTargetClass),                         \
    .class_init = target_info_qom_class_init,                               \
    .abstract = false,                                                      \
} };                                                                        \
DEFINE_TYPES(target_info_qom_target_type_info)

#endif /* CONFIG_USER_ONLY */

#endif /* TARGET_INFO_DEF_H */
