/*
 * QEMU single Latching Switch device
 *
 * Copyright (C) 2022 Jian Zhang <zhangjian.3032@bytedance.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/misc/latching_switch.h"
#include "trace.h"

static void toggle_output(LatchingSwitchState *s)
{
    s->state = !(s->state);
    qemu_set_irq(s->output, !!(s->state));
}

static void input_handler(void *opaque, int line, int new_state)
{
    LatchingSwitchState *s = LATCHING_SWITCH(opaque);

    assert(line == 0);

    if (s->trigger_edge == TRIGGER_EDGE_FALLING && new_state == 0) {
        toggle_output(s);
    } else if (s->trigger_edge == TRIGGER_EDGE_RISING && new_state == 1) {
        toggle_output(s);
    } else if (s->trigger_edge == TRIGGER_EDGE_BOTH) {
        toggle_output(s);
    }
}

static void latching_switch_reset(DeviceState *dev)
{
    LatchingSwitchState *s = LATCHING_SWITCH(dev);
    /* reset to off */
    s->state = false;
    /* reset to falling edge trigger */
    s->trigger_edge = TRIGGER_EDGE_FALLING;
}

static const VMStateDescription vmstate_latching_switch = {
    .name = TYPE_LATCHING_SWITCH,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(state, LatchingSwitchState),
        VMSTATE_U8(trigger_edge, LatchingSwitchState),
        VMSTATE_END_OF_LIST()
    }
};

static void latching_switch_realize(DeviceState *dev, Error **errp)
{
    LatchingSwitchState *s = LATCHING_SWITCH(dev);

    /* init a input io */
    qdev_init_gpio_in(dev, input_handler, 1);

    /* init a output io */
    qdev_init_gpio_out(dev, &(s->output), 1);
}

static void latching_switch_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Latching Switch";
    dc->vmsd = &vmstate_latching_switch;
    dc->reset = latching_switch_reset;
    dc->realize = latching_switch_realize;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void latching_switch_get_state(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    LatchingSwitchState *s = LATCHING_SWITCH(obj);
    bool value = s->state;

    visit_type_bool(v, name, &value, errp);
}

static void latching_switch_set_state(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    LatchingSwitchState *s = LATCHING_SWITCH(obj);
    bool value;
    Error *err = NULL;

    visit_type_bool(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (value != s->state) {
        toggle_output(s);
    }
}
static void latching_switch_get_trigger_edge(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    LatchingSwitchState *s = LATCHING_SWITCH(obj);
    int value = s->trigger_edge;
    char *p = NULL;

    if (value == TRIGGER_EDGE_FALLING) {
        p = g_strdup("falling");
        visit_type_str(v, name, &p, errp);
    } else if (value == TRIGGER_EDGE_RISING) {
        p = g_strdup("rising");
        visit_type_str(v, name, &p, errp);
    } else if (value == TRIGGER_EDGE_BOTH) {
        p = g_strdup("both");
        visit_type_str(v, name, &p, errp);
    } else {
        error_setg(errp, "Invalid trigger edge value");
    }
    g_free(p);
}

static void latching_switch_set_trigger_edge(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    LatchingSwitchState *s = LATCHING_SWITCH(obj);
    char *value;
    Error *err = NULL;

    visit_type_str(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (strcmp(value, "falling") == 0) {
        s->trigger_edge = TRIGGER_EDGE_FALLING;
    } else if (strcmp(value, "rising") == 0) {
        s->trigger_edge = TRIGGER_EDGE_RISING;
    } else if (strcmp(value, "both") == 0) {
        s->trigger_edge = TRIGGER_EDGE_BOTH;
    } else {
        error_setg(errp, "Invalid trigger edge type: %s", value);
    }
}

static void latching_switch_init(Object *obj)
{
    LatchingSwitchState *s = LATCHING_SWITCH(obj);

    s->state = false;
    s->trigger_edge = TRIGGER_EDGE_FALLING;

    object_property_add(obj, "state", "bool",
                        latching_switch_get_state,
                        latching_switch_set_state,
                        NULL, NULL);
    object_property_add(obj, "trigger-edge", "string",
                        latching_switch_get_trigger_edge,
                        latching_switch_set_trigger_edge,
                        NULL, NULL);
}

static const TypeInfo latching_switch_info = {
    .name = TYPE_LATCHING_SWITCH,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(LatchingSwitchState),
    .class_init = latching_switch_class_init,
    .instance_init = latching_switch_init,
};

static void latching_switch_register_types(void)
{
    type_register_static(&latching_switch_info);
}

type_init(latching_switch_register_types);

LatchingSwitchState *latching_switch_create_simple(Object *parentobj,
                                                   bool state,
                                                   uint8_t trigger_edge)
{
    static const char *name = "latching-switch";
    DeviceState *dev;

    dev = qdev_new(TYPE_LATCHING_SWITCH);

    qdev_prop_set_bit(dev, "state", state);

    if (trigger_edge == TRIGGER_EDGE_FALLING) {
        qdev_prop_set_string(dev, "trigger-edge", "falling");
    } else if (trigger_edge == TRIGGER_EDGE_RISING) {
        qdev_prop_set_string(dev, "trigger-edge", "rising");
    } else if (trigger_edge == TRIGGER_EDGE_BOTH) {
        qdev_prop_set_string(dev, "trigger-edge", "both");
    } else {
        error_report("Invalid trigger edge value");
        exit(1);
    }

    object_property_add_child(parentobj, name, OBJECT(dev));
    qdev_realize_and_unref(dev, NULL, &error_fatal);

    return LATCHING_SWITCH(dev);
}
