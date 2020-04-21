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
#include "qapi/qapi-commands-misc.h"
#include "qapi/error.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"

static int query_temperature_sensors_foreach(Object *obj, void *opaque)
{
    TemperatureSensorList **list = opaque;
    TempSensor *sensor;
    TempSensorClass *k;

    if (!object_dynamic_cast(obj, TYPE_TEMPSENSOR_INTERFACE)) {
        return 0;
    }

    k = TEMPSENSOR_INTERFACE_GET_CLASS(obj);
    if (!k->get_temperature) {
        return 0;
    }

    sensor = TEMPSENSOR_INTERFACE(obj);
    for (size_t i = 0; i < k->sensor_count; i++) {
        TemperatureSensorList *info = g_malloc0(sizeof(*info));
        TemperatureSensor *value = g_malloc0(sizeof(*value));

        if (k->get_name) {
            value->name = g_strdup(k->get_name(sensor, i));
        } else {
            value->name = g_strdup_printf("%s-%zu",
                                          object_get_typename(obj), i);
        }
        value->temperature = k->get_temperature(sensor, i);

        info->value = value;
        info->next = *list;
        *list = info;
    }

    return 0;
}

TemperatureSensorList *qmp_query_temperature_sensors(Error **errp)
{
    TemperatureSensorList *list = NULL;

    object_child_foreach_recursive(object_get_root(),
                                   query_temperature_sensors_foreach,
                                   &list);
    return list;
}

void hmp_info_temp(Monitor *mon, const QDict *qdict)
{
    TemperatureSensorList *list, *sensor;
    Error *err = NULL;

    list = qmp_query_temperature_sensors(&err);
    if (!list) {
        monitor_printf(mon, "No temperature sensors\n");
        return;
    }
    if (err) {
        monitor_printf(mon, "Error while getting temperatures: %s\n",
                       error_get_pretty(err));
        error_free(err);
        goto out;
    }

    monitor_printf(mon, "Temperatures (in C):\n");
    for (sensor = list; sensor; sensor = sensor->next) {
        monitor_printf(mon, "%-33s %6.2f\n", sensor->value->name,
                       sensor->value->temperature);
    }

out:
    qapi_free_TemperatureSensorList(list);
}

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
