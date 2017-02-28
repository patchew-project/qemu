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

typedef struct ClkList ClkList;
typedef uint64_t QEMUClkRateUpdateCallback(void *opaque, uint64_t rate);

typedef struct QEMUClock {
    /*< private >*/
    Object parent_obj;
    char *name;            /* name of this clock in the device. */
    uint64_t ref_rate;     /* rate of the clock which drive this pin. */
    uint64_t rate;         /* rate of this clock pin. */
    void *opaque;
    QEMUClkRateUpdateCallback *cb;
    QLIST_HEAD(, ClkList) bound;
} QEMUClock;

struct ClkList {
    QEMUClock *clk;
    QLIST_ENTRY(ClkList) node;
};

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

/**
 * qemu_clk_bind:
 * @out: the clock output.
 * @in: the clock input.
 *
 * Connect the clock together. This is unidirectional so a
 * qemu_clk_update_rate will go from @out to @in.
 *
 */
void qemu_clk_bind(QEMUClock *out, QEMUClock *in);

/**
 * qemu_clk_unbind:
 * @out: the clock output.
 * @in: the clock input.
 *
 * Disconnect the clocks if they were bound together.
 *
 */
void qemu_clk_unbind(QEMUClock *out, QEMUClock *in);

/**
 * qemu_clk_update_rate:
 * @clk: the clock to update.
 * @rate: the new rate in Hz.
 *
 * Update the @clk to the new @rate.
 *
 */
void qemu_clk_update_rate(QEMUClock *clk, uint64_t rate);

/**
 * qemu_clk_refresh:
 * @clk: the clock to be refreshed.
 *
 * If a model alters the topology of a clock tree, it must call this function on
 * the clock source to refresh the clock tree.
 *
 */
void qemu_clk_refresh(QEMUClock *clk);

/**
 * qemu_clk_set_callback:
 * @clk: the clock associated to the callback.
 * @cb: the function which is called when a refresh happen on the clock @clk.
 * @opaque: the opaque data passed to the callback.
 *
 * Set the callback @cb which will be called when the clock @clk is updated.
 *
 */
void qemu_clk_set_callback(QEMUClock *clk,
                           QEMUClkRateUpdateCallback *cb,
                           void *opaque);

#endif /* QEMU_CLOCK_H */
