/*
 *
 * Copyright (c) 2015 Linaro Limited
 * Copyright (c) 2020 Huawei.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_ARM_MICROVM_H
#define QEMU_ARM_MICROVM_H

#include "hw/arm/arm.h"

typedef struct {
    ArmMachineClass parent;
} MicrovmMachineClass;

typedef struct {
    ArmMachineState parent;
} MicrovmMachineState;

#define TYPE_MICROVM_MACHINE   MACHINE_TYPE_NAME("microvm")
#define MICROVM_MACHINE(obj) \
    OBJECT_CHECK(MicrovmMachineState, (obj), TYPE_MICROVM_MACHINE)
#define MICROVM_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MicrovmMachineClass, obj, TYPE_MICROVM_MACHINE)
#define MICROVM_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(MicrovmMachineClass, klass, TYPE_MICROVM_MACHINE)

#endif /* QEMU_ARM_MICROVM_H */
