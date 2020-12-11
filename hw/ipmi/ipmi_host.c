/*
 * IPMI Host emulation
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "hw/ipmi/ipmi_host.h"
#include "hw/ipmi/ipmi_responder.h"

static TypeInfo ipmi_responder_type_info = {
    .name = TYPE_IPMI_RESPONDER,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(IPMIResponderClass),
};

static TypeInfo ipmi_host_type_info = {
    .name = TYPE_IPMI_HOST,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(IPMIHost),
    .abstract = true,
    .class_size = sizeof(IPMIHostClass),
};

static void ipmi_register_types(void)
{
    type_register_static(&ipmi_responder_type_info);
    type_register_static(&ipmi_host_type_info);
}

type_init(ipmi_register_types)
