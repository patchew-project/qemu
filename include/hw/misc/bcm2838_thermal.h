/*
 * BCM2838 dummy thermal sensor
 *
 * Copyright (C) 2022 Maksim Kopusov <maksim.kopusov@auriga.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2838_THERMAL_H
#define BCM2838_THERMAL_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2838_THERMAL "bcm2838-thermal"
OBJECT_DECLARE_SIMPLE_TYPE(Bcm2838ThermalState, BCM2838_THERMAL)

struct Bcm2838ThermalState {
    SysBusDevice busdev;
    MemoryRegion iomem;
};

#endif /* BCM2838_THERMAL_H */
