/*
 * QEMU authorization framework
 *
 * Copyright (c) 2016 Red Hat, Inc.
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
 */

#include "qemu/osdep.h"
#include "qemu/authz.h"

bool qauthz_is_allowed(QAuthZ *authz,
                       const char *identity,
                       Error **errp)
{
    QAuthZClass *cls = QAUTHZ_GET_CLASS(authz);

    return cls->is_allowed(authz, identity, errp);
}

static const TypeInfo authz_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QAUTHZ,
    .instance_size = sizeof(QAuthZ),
    .class_size = sizeof(QAuthZClass),
    .abstract = true,
};

static void qauthz_register_types(void)
{
    type_register_static(&authz_info);
}

type_init(qauthz_register_types)

