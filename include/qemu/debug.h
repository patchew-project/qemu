/*
 * Copyright (c) 2023 Nicolas Eder
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef QEMU_DEBUG_H
#define QEMU_DEBUG_H

#include "qom/object.h"
#include "qemu/typedefs.h"

struct DebugClass {
    ObjectClass parent_class;
    void (*set_stop_cpu)(CPUState *cpu);
};

struct DebugState {
    Object parent_obj;
};

#define TYPE_DEBUG "debug"
OBJECT_DECLARE_TYPE(DebugState, DebugClass, DEBUG)

#endif /* QEMU_DEBUG_H */
