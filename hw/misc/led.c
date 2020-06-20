/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/misc/led.h"
#include "hw/irq.h"
#include "trace.h"

static const char *led_color(LEDColor color)
{
    static const char *color_name[LED_COLOR_COUNT] = {
        [LED_COLOR_RED] = "red",
        [LED_COLOR_ORANGE] = "orange",
        [LED_COLOR_AMBER] = "amber",
        [LED_COLOR_YELLOW] = "yellow",
        [LED_COLOR_GREEN] = "green",
        [LED_COLOR_BLUE] = "blue",
        [LED_COLOR_VIOLET] = "violet", /* PURPLE */
        [LED_COLOR_WHITE] = "white",
    };
    return color_name[color] ? color_name[color] : "unknown";
}

void led_set_intensity(LEDState *s, uint16_t new_intensity)
{
    trace_led_set_intensity(s->description ? s->description : "n/a",
                            s->color, new_intensity);
    if (new_intensity != s->current_intensity) {
        trace_led_change_intensity(s->description ? s->description : "n/a",
                                   s->color,
                                   s->current_intensity, new_intensity);
    }
    s->current_intensity = new_intensity;
}

void led_set_state(LEDState *s, bool is_on)
{
    led_set_intensity(s, is_on ? UINT16_MAX : 0);
}

static void gpio_led_set(void *opaque, int line, int new_state)
{
    LEDState *s = LED(opaque);

    assert(line == 0);
    led_set_state(s, !!new_state);
}

static void led_reset(DeviceState *dev)
{
    LEDState *s = LED(dev);

    led_set_intensity(s, s->reset_intensity);
}

static const VMStateDescription vmstate_led = {
    .name = TYPE_LED,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void led_realize(DeviceState *dev, Error **errp)
{
    LEDState *s = LED(dev);

    if (s->color == NULL) {
        error_setg(errp, "property 'color' not specified");
        return;
    }

    qdev_init_gpio_in(DEVICE(s), gpio_led_set, 1);
}

static Property led_properties[] = {
    DEFINE_PROP_STRING("color", LEDState, color),
    DEFINE_PROP_STRING("description", LEDState, description),
    DEFINE_PROP_UINT16("reset_intensity", LEDState, reset_intensity, 0),
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
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(LEDState),
    .class_init = led_class_init
};

static void led_register_types(void)
{
    type_register_static(&led_info);
}

type_init(led_register_types)

DeviceState *create_led(Object *parentobj,
                        LEDColor color,
                        const char *description,
                        uint16_t reset_intensity)
{
    DeviceState *dev;
    char *name;

    assert(description);
    dev = qdev_new(TYPE_LED);
    qdev_prop_set_uint16(dev, "reset_intensity", reset_intensity);
    qdev_prop_set_string(dev, "color", led_color(color));
    qdev_prop_set_string(dev, "description", description);
    name = g_ascii_strdown(description, -1);
    name = g_strdelimit(name, " #", '-');
    object_property_add_child(parentobj, name, OBJECT(dev));
    g_free(name);
    qdev_realize_and_unref(dev, NULL, &error_fatal);

    return dev;
}

DeviceState *create_led_by_gpio_id(Object *parentobj,
                                   DeviceState *gpio_dev, unsigned gpio_id,
                                   LEDColor color,
                                   const char *description,
                                   uint16_t reset_intensity)
{
    DeviceState *dev;

    dev = create_led(parentobj, color, description, reset_intensity);
    qdev_connect_gpio_out(gpio_dev, gpio_id, qdev_get_gpio_in(dev, 0));

    return dev;
}
