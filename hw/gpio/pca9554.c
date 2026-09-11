/*
 * PCA9554 I/O port
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/pca9554.h"
#include "hw/gpio/pca9554_regs.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/qapi-types-machine.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qapi-type-infos-machine.h"
#include "trace.h"
#include "qom/object.h"

struct PCA9554Class {
    /*< private >*/
    I2CSlaveClass parent_class;
    /*< public >*/

    uint8_t pin_count;
};
typedef struct PCA9554Class PCA9554Class;

DECLARE_CLASS_CHECKERS(PCA9554Class, PCA9554,
                       TYPE_PCA9554)

QEMU_BUILD_BUG_ON(PCA9554_PIN_STATE_LOW != 0x0);
QEMU_BUILD_BUG_ON(PCA9554_PIN_STATE_HIGH != 0x1);

static void pca9554_update_pin_input(PCA9554State *s)
{
    PCA9554Class *pc = PCA9554_GET_CLASS(s);
    int i;
    uint8_t config = s->regs[PCA9554_CONFIG];
    uint8_t output = s->regs[PCA9554_OUTPUT];

    for (i = 0; i < pc->pin_count; i++) {
        uint8_t bit_mask = 1 << i;
        uint8_t old_value = s->regs[PCA9554_INPUT] & bit_mask;
        uint8_t new_value;

        if (config & bit_mask) {
            /*
             * Input: the pin is Hi-Z with a pull-up, so it reads high
             * unless an external device drives it low.
             */
            if (s->ext_state[i] == PCA9554_PIN_STATE_LOW) {
                s->regs[PCA9554_INPUT] &= ~bit_mask;
            } else {
                s->regs[PCA9554_INPUT] |= bit_mask;
            }
        } else {
            /* Output: the push-pull stage drives the output register level. */
            s->regs[PCA9554_INPUT] = (s->regs[PCA9554_INPUT] & ~bit_mask) |
                                     (output & bit_mask);
        }

        /* drive the per-pin GPIO output only if the pin level changed */
        new_value = s->regs[PCA9554_INPUT] & bit_mask;
        if (new_value != old_value) {
            qemu_set_irq(s->gpio_out[i], !!new_value);
        }
    }
}

static uint8_t pca9554_read(PCA9554State *s, uint8_t reg)
{
    switch (reg) {
    case PCA9554_INPUT:
        return s->regs[PCA9554_INPUT] ^ s->regs[PCA9554_POLARITY];
    case PCA9554_OUTPUT:
    case PCA9554_POLARITY:
    case PCA9554_CONFIG:
        return s->regs[reg];
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read to register %d\n",
                      __func__, reg);
        return 0xFF;
    }
}

static void pca9554_write(PCA9554State *s, uint8_t reg, uint8_t data)
{
    PCA9554Class *pc = PCA9554_GET_CLASS(s);
    uint8_t pin_mask = (1 << pc->pin_count) - 1;

    /* Variants narrower than 8 bits ignore the unimplemented upper pins. */
    data &= pin_mask;

    switch (reg) {
    case PCA9554_OUTPUT:
    case PCA9554_CONFIG:
        s->regs[reg] = data;
        pca9554_update_pin_input(s);
        break;
    case PCA9554_POLARITY:
        s->regs[reg] = data;
        break;
    case PCA9554_INPUT:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected write to register %d\n",
                      __func__, reg);
    }
}

static uint8_t pca9554_recv(I2CSlave *i2c)
{
    PCA9554State *s = PCA9554(i2c);

    return pca9554_read(s, s->pointer & 0x3);
}

static int pca9554_send(I2CSlave *i2c, uint8_t data)
{
    PCA9554State *s = PCA9554(i2c);

    /* First byte sent by is the register address */
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else {
        pca9554_write(s, s->pointer & 0x3, data);
    }

    return 0;
}

static int pca9554_event(I2CSlave *i2c, enum i2c_event event)
{
    PCA9554State *s = PCA9554(i2c);

    s->len = 0;
    return 0;
}

static void pca9554_set_ext_state(PCA9554State *s, int pin, int level)
{
    if (s->ext_state[pin] != level) {
        s->ext_state[pin] = level;
        pca9554_update_pin_input(s);
    }
}

/*
 * Report the physical pin level. The input register is kept in sync
 * by pca9554_update_pin_input(): output pins mirror the OUTPUT
 * register and input pins reflect the externally driven (or
 * pulled-up) level, so it holds the wire level regardless of the
 * configured direction.
 */
#define DEFINE_PIN_ACCESSORS(n)                                              \
static int prop_get_pin##n(Object *obj, Error **errp)                        \
{                                                                            \
    PCA9554State *s = PCA9554(obj);                                          \
    return (s->regs[PCA9554_INPUT] >> (n)) & 0x1;                            \
}                                                                            \
static void prop_set_pin##n(Object *obj, int val, Error **errp)              \
{                                                                            \
    PCA9554State *s = PCA9554(obj);                                          \
    if (s->hw_dir) {                                                         \
        if (!((s->regs[PCA9554_CONFIG] >> (n)) & 0x1)) {                     \
            qemu_log_mask(LOG_UNIMP,                                         \
                          "%s: pin %d configured as output,"                 \
                          " ignoring set\n",                                 \
                          s->description, (n));                              \
            return;                                                          \
        }                                                                    \
        pca9554_set_ext_state(s, (n), val != PCA9554_PIN_STATE_LOW);         \
    } else {                                                                 \
        /* Legacy behavior: force output mode and drive */                   \
        uint8_t mask = 0x1 << (n);                                           \
        int v = pca9554_read(s, PCA9554_OUTPUT);                             \
        if (val == PCA9554_PIN_STATE_LOW) {                                  \
            v &= ~mask;                                                      \
        } else {                                                             \
            v |= mask;                                                       \
        }                                                                    \
        pca9554_write(s, PCA9554_OUTPUT, v);                                 \
        v = pca9554_read(s, PCA9554_CONFIG);                                 \
        v &= ~mask;                                                          \
        pca9554_write(s, PCA9554_CONFIG, v);                                 \
    }                                                                        \
}

QEMU_REPEAT(PCA9554_PIN_COUNT, DEFINE_PIN_ACCESSORS)

#define PIN_ENUM_PROP(n) {                                              \
    .name = "pin" #n,                                                   \
    .default_value = -1,                                                \
    .qapi_type = &Pca9554PinState_type_info,                            \
    .get = prop_get_pin##n,                                             \
    .set = prop_set_pin##n,                                             \
},

static const QapiEnumProp pin_enum_props[] = {
    QEMU_REPEAT(PCA9554_PIN_COUNT, PIN_ENUM_PROP)
};

static const VMStateDescription pca9554_vmstate = {
    .name = "PCA9554",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, PCA9554State),
        VMSTATE_UINT8(pointer, PCA9554State),
        VMSTATE_UINT8_ARRAY(regs, PCA9554State, PCA9554_NR_REGS),
        VMSTATE_UINT8_ARRAY(ext_state, PCA9554State, PCA9554_PIN_COUNT),
        VMSTATE_I2C_SLAVE(i2c, PCA9554State),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9554_reset(DeviceState *dev)
{
    PCA9554State *s = PCA9554(dev);
    PCA9554Class *pc = PCA9554_GET_CLASS(s);
    uint8_t pin_mask = (1 << pc->pin_count) - 1;

    s->regs[PCA9554_INPUT] = pin_mask;
    s->regs[PCA9554_OUTPUT] = pin_mask;
    s->regs[PCA9554_POLARITY] = 0x0; /* No pins are inverted */
    s->regs[PCA9554_CONFIG] = pin_mask; /* All pins are inputs */

    memset(s->ext_state, PCA9554_PIN_STATE_HIGH, pc->pin_count);
    pca9554_update_pin_input(s);

    s->pointer = 0x0;
    s->len = 0;
}

static void pca9554_initfn(Object *obj)
{
    PCA9554Class *pc = PCA9554_GET_CLASS(obj);

    for (int pin = 0; pin < pc->pin_count; pin++) {
        object_property_add_qapi_enum(obj, &pin_enum_props[pin]);
    }
}

static void pca9554_gpio_in_handler(void *opaque, int pin, int level)
{
    PCA9554State *s = PCA9554(opaque);
    PCA9554Class *pc = PCA9554_GET_CLASS(s);

    assert((pin >= 0) && (pin < pc->pin_count));
    pca9554_set_ext_state(s, pin, level);
}

static void pca9554_realize(DeviceState *dev, Error **errp)
{
    PCA9554State *s = PCA9554(dev);
    PCA9554Class *pc = PCA9554_GET_CLASS(s);

    if (!s->description) {
        s->description = g_strdup(object_get_typename(OBJECT(dev)));
    }

    qdev_init_gpio_out(dev, s->gpio_out, pc->pin_count);
    qdev_init_gpio_in(dev, pca9554_gpio_in_handler, pc->pin_count);
}

static const Property pca9554_properties[] = {
    DEFINE_PROP_STRING("description", PCA9554State, description),
    DEFINE_PROP_BOOL("hw-dir", PCA9554State, hw_dir, false),
};

static void pca9554_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    PCA9554Class *pc = PCA9554_CLASS(klass);

    k->event = pca9554_event;
    k->recv = pca9554_recv;
    k->send = pca9554_send;
    dc->realize = pca9554_realize;
    device_class_set_legacy_reset(dc, pca9554_reset);
    dc->vmsd = &pca9554_vmstate;
    device_class_set_props(dc, pca9554_properties);

    pc->pin_count = PCA9554_PIN_COUNT;
}

static void pca9536_class_init(ObjectClass *klass, const void *data)
{
    PCA9554Class *pc = PCA9554_CLASS(klass);

    pc->pin_count = PCA9536_PIN_COUNT;
}

static const TypeInfo pca9554_types[] = {
    {
        .name          = TYPE_PCA9554,
        .parent        = TYPE_I2C_SLAVE,
        .instance_init = pca9554_initfn,
        .instance_size = sizeof(PCA9554State),
        .class_init    = pca9554_class_init,
        .class_size    = sizeof(PCA9554Class),
        .abstract      = false,
    },
    {
        .name          = TYPE_PCA9536,
        .parent        = TYPE_PCA9554,
        .class_init    = pca9536_class_init,
    }
};

DEFINE_TYPES(pca9554_types);
