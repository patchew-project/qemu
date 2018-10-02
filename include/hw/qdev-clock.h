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

/**
 * ClockInitElem:
 * @name: name of the clock (can't be NULL)
 * @output: indicates whether the clock is input or output
 * @callback: for inputs, optional callback to be called on clock's update
 * with device as opaque
 * @offset: optional offset to store the clock pointer in device'state
 * structure (0 means unused)
 */
struct ClockPortInitElem {
    const char *name;
    bool output;
    ClockCallback *callback;
    size_t offset;
};

#define clock_offset_value(_type, _devstate, _field) \
    (offsetof(_devstate, _field) + \
     type_check(_type *, typeof_field(_devstate, _field)))

#define QDEV_CLOCK(_output, _type, _devstate, _field, _callback) { \
    .name = (stringify(_field)), \
    .output = _output, \
    .callback = _callback, \
    .offset = clock_offset_value(_type, _devstate, _field), \
}

/**
 * QDEV_CLOCK_(IN|OUT):
 * @_devstate: structure type. @dev argument of qdev_init_clocks below must be
 * a pointer to that same type.
 * @_field: a field in @_devstate (must be ClockIn* or ClockOut*)
 * @_callback: (for input only) callback (or NULL) to be called with the device
 * state as argument
 *
 * The name of the clock will be derived from @_field
 */
#define QDEV_CLOCK_IN(_devstate, _field, _callback) \
    QDEV_CLOCK(false, ClockIn, _devstate, _field, _callback)

#define QDEV_CLOCK_OUT(_devstate, _field) \
    QDEV_CLOCK(true, ClockOut, _devstate, _field, NULL)

/**
 * QDEV_CLOCK_IN_NOFIELD:
 * @_name: name of the clock
 * @_callback: callback (or NULL) to be called with the device state as argument
 */
#define QDEV_CLOCK_IN_NOFIELD(_name, _callback) { \
    .name = _name, \
    .output = false, \
    .callback = _callback, \
    .offset = 0, \
}

#define QDEV_CLOCK_END { .name = NULL }

typedef struct ClockPortInitElem ClockPortInitArray[];

/**
 * qdev_init_clocks:
 * @dev: the device to add clocks
 * @clocks: a QDEV_CLOCK_END-terminated array which contains the
 * clocks information.
 */
void qdev_init_clocks(DeviceState *dev, const ClockPortInitArray clocks);

#endif /* QDEV_CLOCK_H */
