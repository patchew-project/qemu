/*
 * Fixed clock
 *
 *  Copyright (C) 2016 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Frederic Konrad   <fred.konrad@greensocs.com>
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

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "hw/misc/fixed-clock.h"
#include "qemu/qemu-clock.h"
#include "qapi/error.h"

#ifndef DEBUG_FIXED_CLOCK
#define DEBUG_FIXED_CLOCK 0
#endif

#define DPRINTF(fmt, ...) do {                                               \
    if (DEBUG_FIXED_CLOCK) {                                                 \
        qemu_log(__FILE__": " fmt , ## __VA_ARGS__);                         \
    }                                                                        \
} while (0);

typedef struct {
    DeviceState parent_obj;

    uint32_t rate;
    QEMUClock out;
} FixedClock;

static Property fixed_clock_properties[] = {
    DEFINE_PROP_UINT32("rate", FixedClock, rate, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void fixed_clock_realizefn(DeviceState *dev, Error **errp)
{
    FixedClock *s = FIXED_CLOCK(dev);

    qemu_clk_update_rate(&s->out, s->rate);
}

static void fixed_clock_instance_init(Object *obj)
{
    FixedClock *s = FIXED_CLOCK(obj);

    object_initialize(&s->out, sizeof(s->out), TYPE_CLOCK);
    qemu_clk_device_add_clock(DEVICE(obj), &s->out, "clk_out");
}

static void fixed_clock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = fixed_clock_realizefn;
    dc->props = fixed_clock_properties;
}

static const TypeInfo fixed_clock_info = {
    .name          = TYPE_FIXED_CLOCK,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(FixedClock),
    .instance_init = fixed_clock_instance_init,
    .class_init    = fixed_clock_class_init,
};

static void fixed_clock_register_types(void)
{
    type_register_static(&fixed_clock_info);
}

type_init(fixed_clock_register_types);
