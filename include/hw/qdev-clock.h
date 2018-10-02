#ifndef QDEV_CLOCK_H
#define QDEV_CLOCK_H

#include "hw/clock-port.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"

/**
 * qdev_init_clock_in:
 * @dev: the device in which to add a clock
 * @name: the name of the clock (can't be NULL).
 * @callback: optional callback to be called on update or NULL.
 * @opaque:   argument for the callback
 * @returns: a pointer to the newly added clock
 *
 * Add a input clock to device @dev as a clock named @name.
 * This adds a child<> property.
 * The callback will be called with @dev as opaque parameter.
 */
ClockIn *qdev_init_clock_in(DeviceState *dev, const char *name,
                            ClockCallback *callback, void *opaque);

/**
 * qdev_init_clock_out:
 * @dev: the device to add a clock to
 * @name: the name of the clock (can't be NULL).
 * @callback: optional callback to be called on update or NULL.
 * @returns: a pointer to the newly added clock
 *
 * Add a output clock to device @dev as a clock named @name.
 * This adds a child<> property.
 */
ClockOut *qdev_init_clock_out(DeviceState *dev, const char *name);

/**
 * qdev_pass_clock:
 * @dev: the device to forward the clock to
 * @name: the name of the clock to be added (can't be NULL)
 * @container: the device which already has the clock
 * @cont_name: the name of the clock in the container device
 *
 * Add a clock @name to @dev which forward to the clock @cont_name in @container
 */
void qdev_pass_clock(DeviceState *dev, const char *name,
        DeviceState *container, const char *cont_name);

/**
 * qdev_connect_clock:
 * @dev: the drived clock device.
 * @name: the drived clock name.
 * @driver: the driving clock device.
 * @driver_name: the driving clock name.
 * @errp: error report
 *
 * Setup @driver_name output clock of @driver to drive @name input clock of
 * @dev. Errors are trigerred if clock does not exists
 */
void qdev_connect_clock(DeviceState *dev, const char *name,
                        DeviceState *driver, const char *driver_name,
                        Error **errp);

#endif /* QDEV_CLOCK_H */
