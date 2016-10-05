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
#define QEMU_CLOCK(obj) OBJECT_CHECK(struct qemu_clk, (obj), TYPE_CLOCK)

typedef struct qemu_clk {
    /*< private >*/
    Object parent_obj;
    char *name;            /* name of this clock in the device. */
} *qemu_clk;

/**
 * qemu_clk_attach_to_device:
 * @dev: the device on which the clock need to be attached.
 * @clk: the clock which need to be attached.
 * @name: the name of the clock can't be NULL.
 *
 * Attach @clk named @name to the device @dev.
 *
 */
void qemu_clk_attach_to_device(DeviceState *dev, qemu_clk clk,
                               const char *name);

/**
 * qemu_clk_get_pin:
 * @dev: the device which contain the clock.
 * @name: the name of the clock.
 *
 * Get the clock named @name located in the device @dev, or NULL if not found.
 *
 * Returns the clock named @name contained in @dev.
 */
qemu_clk qemu_clk_get_pin(DeviceState *dev, const char *name);

#endif /* QEMU_CLOCK_H */
