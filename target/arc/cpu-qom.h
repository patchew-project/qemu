/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef QEMU_ARC_CPU_QOM_H
#define QEMU_ARC_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_ARC_CPU            "arc-cpu"

#define ARC_CPU_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(ARCCPUClass, (klass), TYPE_ARC_CPU)
#define ARC_CPU(obj)                            \
    OBJECT_CHECK(ARCCPU, (obj), TYPE_ARC_CPU)
#define ARC_CPU_GET_CLASS(obj)                          \
    OBJECT_GET_CLASS(ARCCPUClass, (obj), TYPE_ARC_CPU)

/*
 *  ARCCPUClass:
 *  @parent_realize: The parent class' realize handler.
 *  @parent_reset: The parent class' reset handler.
 *
 *  A ARC CPU model.
 */
typedef struct ARCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
} ARCCPUClass;

typedef struct ARCCPU ARCCPU;

#endif
