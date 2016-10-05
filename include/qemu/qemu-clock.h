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

typedef uint64_t (*qemu_clk_on_rate_update_cb)(void *opaque, uint64_t rate);

#define TYPE_CLOCK "qemu-clk"
#define QEMU_CLOCK(obj) OBJECT_CHECK(struct qemu_clk, (obj), TYPE_CLOCK)

typedef struct ClkList ClkList;

typedef struct qemu_clk {
    /*< private >*/
    Object parent_obj;
    char *name;            /* name of this clock in the device. */
    uint64_t in_rate;      /* rate of the clock which drive this pin. */
    uint64_t out_rate;     /* rate of this clock pin. */
    void *opaque;
    qemu_clk_on_rate_update_cb cb;
    QLIST_HEAD(, ClkList) bound;
} *qemu_clk;

struct ClkList {
    qemu_clk clk;
    QLIST_ENTRY(ClkList) node;
};

typedef struct ClockInitElement {
    const char *name;      /* Name to give to the clock. */
    size_t offset;         /* Offset of the qemu_clk field in the object. */
    qemu_clk_on_rate_update_cb cb;
} ClockInitElement;

#define DEVICE_CLOCK(_state, _field, _cb) {                                  \
    .name = #_field,                                                         \
    .offset = offsetof(_state, _field),                                      \
    .cb = _cb                                                                \
}

#define DEVICE_CLOCK_END() {                                                 \
    .name = NULL                                                             \
}

/**
 * qemu_clk_init_device:
 * @obj: the Object which need to be initialized.
 * @array: the array of ClockInitElement to be used.
 */
void qemu_clk_init_device(Object *obj, ClockInitElement *array);

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

/**
 * qemu_clk_bind_clock:
 * @out: the clock output.
 * @in: the clock input.
 *
 * Connect the clock together. This is unidirectional so a
 * qemu_clk_update_rate will go from @out to @in.
 *
 */
void qemu_clk_bind_clock(qemu_clk out, qemu_clk in);

/**
 * qemu_clk_unbound:
 * @out: the clock output.
 * @in: the clock input.
 *
 * Disconnect the clocks if they were bound together.
 *
 */
void qemu_clk_unbind(qemu_clk out, qemu_clk in);

/**
 * qemu_clk_update_rate:
 * @clk: the clock to update.
 * @rate: the new rate.
 *
 * Update the @clk to the new @rate.
 *
 */
void qemu_clk_update_rate(qemu_clk clk, uint64_t rate);

/**
 * qemu_clk_refresh:
 * @clk: the clock to be refreshed.
 *
 * If a model alters the topology of a clock tree, it must call this function
 * to refresh the clock tree.
 *
 */
void qemu_clk_refresh(qemu_clk clk);

/**
 * qemu_clk_set_callback:
 * @clk: the clock where to set the callback.
 * @cb: the callback to associate to the callback.
 * @opaque: the opaque data passed to the calback.
 *
 */
void qemu_clk_set_callback(qemu_clk clk,
                           qemu_clk_on_rate_update_cb cb,
                           void *opaque);

#endif /* QEMU_CLOCK_H */
