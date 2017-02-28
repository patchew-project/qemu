/*
 * QEMU Clock
 *
 *  Copyright (C) 2016 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Frederic Konrad <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_CLOCK_H
#define QEMU_CLOCK_H

#include "qemu/osdep.h"
#include "qom/object.h"

#define TYPE_CLOCK "qemu-clk"
#define QEMU_CLOCK(obj) OBJECT_CHECK(QEMUClock, (obj), TYPE_CLOCK)

typedef struct QEMUClock {
    /*< private >*/
    Object parent_obj;
    char *name;            /* name of this clock in the device. */
} QEMUClock;

/**
 * qemu_clk_device_add_clock:
 * @dev: the device on which the clock needs to be added.
 * @clk: the clock which needs to be added.
 * @name: the name of the clock can't be NULL.
 *
 * Add @clk to device @dev as a clock named @name.
 *
 */
void qemu_clk_device_add_clock(DeviceState *dev, QEMUClock *clk,
                               const char *name);

/**
 * qemu_clk_device_get_clock:
 * @dev: the device which contains the clock.
 * @name: the name of the clock.
 *
 * Get the clock named @name contained in the device @dev, or NULL if not found.
 *
 * Returns the clock named @name contained in @dev.
 */
QEMUClock *qemu_clk_device_get_clock(DeviceState *dev, const char *name);

#endif /* QEMU_CLOCK_H */
