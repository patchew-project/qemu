/*
 * Generic interface for temperature sensors
 *
 * Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/temp-sensor.h"

static TypeInfo tempsensor_interface_type_info = {
    .name = TYPE_TEMPSENSOR_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(TempSensorClass),
};

static void tempsensor_register_types(void)
{
    type_register_static(&tempsensor_interface_type_info);
}

type_init(tempsensor_register_types)
