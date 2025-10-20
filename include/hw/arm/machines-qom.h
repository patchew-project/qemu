/*
 * QOM type definitions for ARM / Aarch64 machines
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_MACHINES_QOM_H
#define HW_ARM_MACHINES_QOM_H

#include "hw/boards.h"

#define TYPE_TARGET_ARM_MACHINE \
        "target-info-arm-machine"

#define TYPE_TARGET_AARCH64_MACHINE \
        "target-info-aarch64-machine"

extern const InterfaceInfo arm_aarch64_machine_interfaces[];
extern const InterfaceInfo aarch64_machine_interfaces[];

#define DEFINE_MACHINE_ARM_AARCH64(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            arm_aarch64_machine_interfaces)

#define DEFINE_MACHINE_AARCH64(namestr, machine_initfn) \
        DEFINE_MACHINE_WITH_INTERFACE_ARRAY(namestr, machine_initfn, \
                                            aarch64_machine_interfaces)

#endif
