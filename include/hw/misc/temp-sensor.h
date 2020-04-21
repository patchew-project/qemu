/*
 * Generic interface for temperature sensors
 *
 * Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TEMP_SENSOR_H
#define HW_MISC_TEMP_SENSOR_H

#include "qom/object.h"

#define TYPE_TEMPSENSOR_INTERFACE "temp-sensor-interface"
#define TEMPSENSOR_INTERFACE(obj) \
    INTERFACE_CHECK(TempSensor, (obj), TYPE_TEMPSENSOR_INTERFACE)
#define TEMPSENSOR_INTERFACE_CLASS(class) \
    OBJECT_CLASS_CHECK(TempSensorClass, (class), TYPE_TEMPSENSOR_INTERFACE)
#define TEMPSENSOR_INTERFACE_GET_CLASS(class) \
    OBJECT_GET_CLASS(TempSensorClass, (class), TYPE_TEMPSENSOR_INTERFACE)

typedef struct TempSensor TempSensor;

typedef struct TempSensorClass {
    /* <private> */
    InterfaceClass parent;
    /* <public> */

    /** Number of temperature sensors */
    size_t sensor_count;

    /**
     * get_name:
     * @obj: opaque pointer to Object
     * @sensor_id: sensor index
     *
     * Returns: name of a temperature sensor.
     */
    const char *(*get_name)(TempSensor *obj, unsigned sensor_id);

    /**
     * set_temperature:
     * @obj: opaque pointer to Object
     * @sensor_id: sensor index
     * @temp_C: sensor temperature, in degree Celsius
     * @errp: pointer to a NULL-initialized error object
     *
     * Set a sensor temperature.
     *
     * If an error occurs @errp will contain the details
     * (likely temperature out of range).
     */
    void (*set_temperature)(TempSensor *obj,
                            unsigned sensor_id, float temp_C, Error **errp);

    /**
     * get_temperature:
     * @obj: opaque pointer to Object
     * @sensor_id: sensor index
     *
     * Get a sensor temperature.
     *
     * Returns: sensor temperature in °C.
     */
    float (*get_temperature)(TempSensor *obj, unsigned sensor_id);
} TempSensorClass;

#endif /* HW_MISC_TEMP_SENSOR_H */
