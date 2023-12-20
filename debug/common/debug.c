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

#include "qemu/osdep.h"
#include "qemu/debug.h"
#include "qom/object_interfaces.h"

static void debug_instance_init(Object *obj)
{
}

static void debug_finalize(Object *obj)
{
}

static void debug_class_init(ObjectClass *oc, void *data)
{
}

static const TypeInfo debug_info = {
    .name = TYPE_DEBUG,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DebugState),
    .instance_init = debug_instance_init,
    .instance_finalize = debug_finalize,
    .class_size = sizeof(DebugClass),
    .class_init = debug_class_init
};

static void debug_register_types(void)
{
    type_register_static(&debug_info);
}

type_init(debug_register_types);
