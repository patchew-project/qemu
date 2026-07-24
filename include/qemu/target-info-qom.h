/*
 * QEMU target info QOM types
 *
 * Copyright (c) Qualcomm
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_QOM_H
#define QEMU_TARGET_INFO_QOM_H

#include "qemu/target-info-impl.h"
#include "qom/object.h"

#define TYPE_TARGET_INFO "target-info"

#define TYPE_TARGET_SPECIFIC "target-specific"

typedef struct TargetSpecific TargetSpecific;

typedef struct TargetSpecificClass {
    InterfaceClass parent_class;

    bool (*is_available)(void);
} TargetSpecificClass;

#define TARGET_SPECIFIC(obj) \
    INTERFACE_CHECK(TargetSpecific, (obj), TYPE_TARGET_SPECIFIC)
DECLARE_CLASS_CHECKERS(TargetSpecificClass, TARGET_SPECIFIC,
                       TYPE_TARGET_SPECIFIC)

typedef struct TargetInfoQom {
    Object parent_obj;
} TargetInfoQom;

typedef struct TargetInfoQomClass {
    ObjectClass parent_class;
    const TargetInfo *target_info;
} TargetInfoQomClass;

OBJECT_DECLARE_TYPE(TargetInfoQom, TargetInfoQomClass, TARGET_INFO)

void target_info_qom_set_target(void);

#endif /* QEMU_TARGET_INFO_QOM_H */
