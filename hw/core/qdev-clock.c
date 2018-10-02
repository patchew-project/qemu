/*
 * Device's clock
 *
 * Copyright GreenSocs 2016-2018
 *
 * Authors:
 *  Frederic Konrad <fred.konrad@greensocs.com>
 *  Damien Hedde <damien.hedde@greensocs.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/qdev-clock.h"
#include "qapi/error.h"

static NamedClockList *qdev_init_clocklist(DeviceState *dev, const char *name,
        bool forward)
{
    NamedClockList *ncl;

    /*
     * The clock path will be computed by the device's realize function call.
     * This is required to ensure the clock's canonical path is right and log
     * messages are meaningfull.
     */
    assert(name);
    assert(!dev->realized);

    ncl = g_malloc0(sizeof(*ncl));
    ncl->name = g_strdup(name);
    ncl->forward = forward;

    QLIST_INSERT_HEAD(&dev->clocks, ncl, node);
    return ncl;
}

ClockOut *qdev_init_clock_out(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;
    Object *clk;

    ncl = qdev_init_clocklist(dev, name, false);

    clk = object_new(TYPE_CLOCK_OUT);

    /* will fail if name already exists */
    object_property_add_child(OBJECT(dev), name, clk, &error_abort);
    object_unref(clk); /* remove the initial ref made by object_new */

    ncl->out = CLOCK_OUT(clk);
    return ncl->out;
}

ClockIn *qdev_init_clock_in(DeviceState *dev, const char *name,
                        ClockCallback *callback, void *opaque)
{
    NamedClockList *ncl;
    Object *clk;

    ncl = qdev_init_clocklist(dev, name, false);

    clk = object_new(TYPE_CLOCK_IN);
    /*
     * the ref initialized by object_new will be cleared during dev finalize.
     * It allows us to safely remove the callback.
     */

    /* will fail if name already exists */
    object_property_add_child(OBJECT(dev), name, clk, &error_abort);

    ncl->in = CLOCK_IN(clk);
    if (callback) {
        clock_set_callback(ncl->in, callback, opaque);
    }
    return ncl->in;
}

static NamedClockList *qdev_get_clocklist(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;

    QLIST_FOREACH(ncl, &dev->clocks, node) {
        if (strcmp(name, ncl->name) == 0) {
            return ncl;
        }
    }

    return NULL;
}

void qdev_pass_clock(DeviceState *dev, const char *name,
                     DeviceState *container, const char *cont_name)
{
    NamedClockList *original_ncl, *ncl;
    Object **clk;

    assert(container && cont_name);

    original_ncl = qdev_get_clocklist(container, cont_name);
    assert(original_ncl); /* clock must exist in origin */

    ncl = qdev_init_clocklist(dev, name, true);

    if (ncl->out) {
        clk = (Object **)&ncl->out;
    } else {
        clk = (Object **)&ncl->in;
    }

    /* will fail if name already exists */
    object_property_add_link(OBJECT(dev), name, object_get_typename(*clk),
            clk, NULL, OBJ_PROP_LINK_STRONG, &error_abort);
}

void qdev_connect_clock(DeviceState *dev, const char *name,
                        DeviceState *driver, const char *driver_name,
                        Error **errp)
{
    NamedClockList *ncl, *drv_ncl;

    assert(dev && name);
    assert(driver && driver_name);

    ncl = qdev_get_clocklist(dev, name);
    if (!ncl || !ncl->in) {
        error_setg(errp, "no input clock '%s' in device", name);
        return;
    }

    drv_ncl = qdev_get_clocklist(driver, driver_name);
    if (!drv_ncl || !drv_ncl->out) {
        error_setg(errp, "no output clock '%s' in driver", driver_name);
        return;
    }

    clock_connect(ncl->in , drv_ncl->out);
}
