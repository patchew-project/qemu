/*
 * NXP PCA I2C GPIO Expanders
 *
 * Low-voltage translating 16-bit I2C/SMBus GPIO expander with interrupt output,
 * reset, and configuration registers
 *
 * Datasheet: https://www.nxp.com/docs/en/data-sheet/PCA6416A.pdf
 *
 * Copyright 2023 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * To assert some input pins before boot, use the following in the board file of
 * the machine:
 *      object_property_set_uint(Object *obj, const char *name,
 *                               uint64_t value, Error **errp);
 * specifying name as "gpio_config" and the value as a bitfield of the inputs
 * e.g. for the pca6416, a value of 0xFFF0, configures pins 0-3 as outputs and
 * 4-15 as inputs.
 * Then using name "gpio_input" with value "0x0F00" would raise GPIOs 8-11.
 *
 * This value can also be set at runtime through qmp externally, or by
 * writing to the config register using i2c. The guest driver should generally
 * control the config register, but exposing it via qmp allows external testing.
 *
 */

#include "qemu/osdep.h"
#include "hw/gpio/pca_i2c_gpio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "trace.h"

/*
 * compare new_output to curr_output and update irq to match new_output
 *
 * The Input port registers (registers 0 and 1) reflect the incoming logic
 * levels of the pins, regardless of whether the pin is defined as an input or
 * an output by the Configuration register.
 */
static void pca_i2c_update_irqs(PCAGPIOState *ps)
{
    PCAGPIOClass *pc = PCA_I2C_GPIO_GET_CLASS(ps);
    uint16_t out_diff = ps->new_output ^ ps->curr_output;
    uint16_t in_diff = ps->new_input ^ ps->curr_input;
    uint16_t mask, pin_i;

    if (in_diff || out_diff) {
        for (int i = 0; i < pc->num_pins; i++) {
            mask = BIT(i);
            /* pin must be configured as an output to be set here */
            if (out_diff & ~ps->config & mask) {
                pin_i = mask & ps->new_output;
                qemu_set_irq(ps->output[i], pin_i > 0);
                ps->curr_output &= ~mask;
                ps->curr_output |= pin_i;
            }

            if (in_diff & mask) {
                ps->curr_input &= ~mask;
                ps->curr_input |= mask & ps->new_input;
            }
        }
        /* make diff = 0 */
        ps->new_input = ps->curr_input;
    }
}

static void pca_i2c_irq_handler(void *opaque, int n, int level)
{
    PCAGPIOState *ps = opaque;
    PCAGPIOClass *pc = PCA_I2C_GPIO_GET_CLASS(opaque);
    uint16_t mask = BIT(n);

    g_assert(n < pc->num_pins);
    g_assert(n >= 0);

    ps->new_input &= ~mask;

    if (level > 0) {
        ps->new_input |= BIT(n);
    }

    pca_i2c_update_irqs(ps);
}

/* slave to master */
static uint8_t _pca953x_recv(I2CSlave *i2c, uint32_t shift)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(i2c);
    uint8_t data;

    switch (ps->command) {
    case PCA953x_INPUT_PORT:
        data = extract16(ps->curr_input, shift, 8);
        break;
    /*
     * i2c reads to the output registers reflect the values written
     * NOT the actual values of the gpios
     */
    case PCA953x_OUTPUT_PORT:
        data = extract16(ps->new_output, shift, 8);
        break;

    case PCA953x_POLARITY_INVERSION_PORT:
        data = extract16(ps->polarity_inv, shift, 8);
        break;

    case PCA953x_CONFIGURATION_PORT:
        data = extract16(ps->config, shift, 8);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: reading from unsupported register 0x%02x",
                      __func__, ps->command);
        data = 0xFF;
        break;
    }

    trace_pca_i2c_recv(DEVICE(ps)->canonical_path, ps->command, shift, data);
    return data;
}

static uint8_t pca6416_recv(I2CSlave *i2c)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(i2c);
    uint32_t shift = ps->command & 1 ? 8 : 0;

    /* Transform command into 4 port equivalent */
    ps->command = ps->command >> 1;

    return _pca953x_recv(i2c, shift);
}

static uint8_t pca953x_recv(I2CSlave *i2c)
{
    return _pca953x_recv(i2c, 0);
}

/* master to slave */
static int _pca953x_send(I2CSlave *i2c, uint32_t shift, uint8_t data)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(i2c);

    if (ps->i2c_cmd) {
        ps->command = data;
        ps->i2c_cmd = false;
        return 0;
    }

    trace_pca_i2c_send(DEVICE(ps)->canonical_path, ps->command, shift, data);

    switch (ps->command) {
    case PCA953x_INPUT_PORT:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: writing to read only reg: 0x%02x",
                      __func__, ps->command);
        break;
    case PCA953x_OUTPUT_PORT:
        ps->new_output = deposit16(ps->new_output, shift, 8, data);
        break;

    case PCA953x_POLARITY_INVERSION_PORT:
        ps->polarity_inv = deposit16(ps->polarity_inv, shift, 8, data);
        break;

    case PCA953x_CONFIGURATION_PORT:
        ps->config = deposit16(ps->config, shift, 8, data);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: writing to unsupported register\n",
                      __func__);
        return -1;
    }

    pca_i2c_update_irqs(ps);
    return 0;
}

static int pca6416_send(I2CSlave *i2c, uint8_t data)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(i2c);
    uint32_t shift = ps->command & 1 ? 8 : 0;

    /* Transform command into 4 port equivalent */
    ps->command = ps->command >> 1;

    return _pca953x_send(i2c, shift, data);
}

static int pca953x_send(I2CSlave *i2c, uint8_t data)
{
    return _pca953x_send(i2c, 0, data);
}

static int pca_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(i2c);

    switch (event) {
    case I2C_START_RECV:
        trace_pca_i2c_event(DEVICE(ps)->canonical_path, "START_RECV");
        break;

    case I2C_START_SEND:
        trace_pca_i2c_event(DEVICE(ps)->canonical_path, "START_SEND");
        ps->i2c_cmd = true;
        break;

    case I2C_FINISH:
        trace_pca_i2c_event(DEVICE(ps)->canonical_path, "FINISH");
        break;

    case I2C_NACK:
        trace_pca_i2c_event(DEVICE(ps)->canonical_path, "NACK");
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s: unknown event 0x%x\n",
                      DEVICE(ps)->canonical_path, __func__, event);
        return -1;
    }

    return 0;
}

static void pca_i2c_config_get(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    visit_type_uint16(v, name, &ps->config, errp);
}

static void pca_i2c_config_set(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    if (!visit_type_uint16(v, name, &ps->config, errp)) {
        return;
    }
    pca_i2c_update_irqs(ps);
}


static void pca_i2c_input_get(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    visit_type_uint16(v, name, &ps->curr_input, errp);
}

static void pca_i2c_input_set(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    if (!visit_type_uint16(v, name, &ps->new_input, errp)) {
        return;
    }
    pca_i2c_update_irqs(ps);
}

static void pca_i2c_output_get(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    visit_type_uint16(v, name, &ps->curr_output, errp);
}

static void pca_i2c_output_set(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    if (!visit_type_uint16(v, name, &ps->new_output, errp)) {
        return;
    }
    pca_i2c_update_irqs(ps);
}

static void pca_i2c_enter_reset(Object *obj, ResetType type)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);

    ps->polarity_inv = 0;
    ps->config = 0;
    ps->new_input = 0;
    ps->new_output = 0;
    ps->command = 0;

    pca_i2c_update_irqs(ps);
}


static const VMStateDescription vmstate_pca_i2c_gpio = {
    .name = TYPE_PCA_I2C_GPIO,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent, PCAGPIOState),
        VMSTATE_UINT16(polarity_inv, PCAGPIOState),
        VMSTATE_UINT16(config, PCAGPIOState),
        VMSTATE_UINT16(curr_input, PCAGPIOState),
        VMSTATE_UINT16(curr_output, PCAGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void pca6416_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    PCAGPIOClass *pc = PCA_I2C_GPIO_CLASS(klass);

    dc->desc = "PCA6416 16-bit I/O expander";
    pc->num_pins = PCA6416_NUM_PINS;

    k->recv = pca6416_recv;
    k->send = pca6416_send;
}

static void pca9538_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    PCAGPIOClass *pc = PCA_I2C_GPIO_CLASS(klass);

    dc->desc = "PCA9538 8-bit I/O expander";
    pc->num_pins = PCA9538_NUM_PINS;

    k->recv = pca953x_recv;
    k->send = pca953x_send;
}

static void pca_i2c_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_pca_i2c_gpio;
    rc->phases.enter = pca_i2c_enter_reset;
    k->event = pca_i2c_event;
}

static void pca_i2c_gpio_init(Object *obj)
{
    PCAGPIOState *ps = PCA_I2C_GPIO(obj);
    PCAGPIOClass *pc = PCA_I2C_GPIO_GET_CLASS(obj);
    DeviceState *dev = DEVICE(obj);

    object_property_add(obj, "gpio_input", "uint16",
                        pca_i2c_input_get,
                        pca_i2c_input_set, NULL, NULL);
    object_property_add(obj, "gpio_output", "uint16",
                        pca_i2c_output_get,
                        pca_i2c_output_set, NULL, NULL);
    object_property_add(obj, "gpio_config", "uint16",
                        pca_i2c_config_get,
                        pca_i2c_config_set, NULL, NULL);
    qdev_init_gpio_in(dev, pca_i2c_irq_handler, pc->num_pins);
    qdev_init_gpio_out(dev, ps->output, pc->num_pins);
}

static const TypeInfo pca_gpio_types[] = {
    {
        .name = TYPE_PCA_I2C_GPIO,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(PCAGPIOState),
        .instance_init = pca_i2c_gpio_init,
        .class_size = sizeof(PCAGPIOClass),
        .class_init = pca_i2c_gpio_class_init,
        .abstract = true,
    },
    {
        .name = TYPE_PCA6416_GPIO,
        .parent = TYPE_PCA_I2C_GPIO,
        .class_init = pca6416_gpio_class_init,
    },
    {
    .name = TYPE_PCA9538_GPIO,
    .parent = TYPE_PCA_I2C_GPIO,
    .class_init = pca9538_gpio_class_init,
    },
};

DEFINE_TYPES(pca_gpio_types);
