/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-events-led.h"
#include "hw/qdev-properties.h"
#include "hw/misc/led.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "trace.h"

static void led_set(void *opaque, int line, int new_state)
{
    LEDState *s = LED(opaque);

    trace_led_set(s->name, s->current_state, new_state);

    /* FIXME QMP rate limite? */
    qapi_event_send_led_status_changed(s->name, new_state
                                                ? LED_STATE_ON : LED_STATE_OFF);
    s->current_state = new_state;
}

static void led_reset(DeviceState *dev)
{
    LEDState *s = LED(dev);

    s->current_state = s->reset_state;
}

static const VMStateDescription vmstate_led = {
    .name = TYPE_LED,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(reset_state, LEDState),
        VMSTATE_END_OF_LIST()
    }
};

static void led_realize(DeviceState *dev, Error **errp)
{
    LEDState *s = LED(dev);

    if (s->name == NULL) {
        error_setg(errp, "property 'name' not specified");
        return;
    }

    qdev_init_gpio_in(DEVICE(s), led_set, 1);
}

static Property led_properties[] = {
    DEFINE_PROP_STRING("name", LEDState, name),
    DEFINE_PROP_UINT8("reset_state", LEDState, reset_state, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void led_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "LED";
    dc->vmsd = &vmstate_led;
    dc->reset = led_reset;
    dc->realize = led_realize;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    device_class_set_props(dc, led_properties);
}

static const TypeInfo led_info = {
    .name = TYPE_LED,
    .parent = TYPE_SYS_BUS_DEVICE, /* FIXME TYPE_DEVICE */
    .instance_size = sizeof(LEDState),
    .class_init = led_class_init
};

static void led_register_types(void)
{
    type_register_static(&led_info);
}

type_init(led_register_types)
