/*
 * Maxim MAX11615 Low-Power 12 bit ADC
 * Models MAX11612,MAX11613,MAX11614,MAX11615,MAX11616,MAX11617
 *
 * Datasheet:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/MAX11612-MAX11617.pdf
 *
 * Copyright 2026 9elements
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/sensor/max11615.h"

#define MAX11615_NUM_CHANNELS 8

struct MAX11615State {
    I2CSlave i2c;

    uint16_t channels[MAX11615_NUM_CHANNELS];

    /* output buffer */
    uint8_t outlen;
    uint8_t outbuf[2];

    /* selected channel for read/write operation */
    uint8_t pointer;
};

struct MAX11615Class {
    I2CSlaveClass parent_class;
};

OBJECT_DECLARE_TYPE(MAX11615State, MAX11615Class, MAX11615)

static void max11615_read(MAX11615State *s)
{
    /* read an ADC channel, first 4 bits must be high */
    uint8_t msb = s->channels[s->pointer] >> 8;
    uint8_t lsb = s->channels[s->pointer] & 0xff;
    s->outbuf[0] = 0b11110000 | (msb & 0b00001111);
    s->outbuf[1] = lsb;
}

static void max11615_write_config_byte(MAX11615State *s, uint8_t data)
{
    trace_max11615_write_config(s->i2c.address, data);

    uint8_t channelSelect = (data >> 1) & 0b1111;

    /* Table 3. Channel Selection (AIN0 ... AIN11) */
    if (channelSelect > 11) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid channel select", __func__);
        channelSelect = 11;
    }
    s->pointer = channelSelect;
}

static void max11615_write_setup_byte(MAX11615State *s, uint8_t data)
{
    trace_max11615_write_setup(s->i2c.address, data);
    /* we ignore the setup byte, not implemented */
}

static int max11615_send(I2CSlave *i2c, uint8_t data)
{
    MAX11615State *s = MAX11615(i2c);
    const uint8_t msb = (data >> 7) & 0b1;

    if (msb) {
        max11615_write_setup_byte(s, data);
    } else {
        max11615_write_config_byte(s, data);
    }

    s->outlen = 0;
    return 0;
}

static uint8_t max11615_recv(I2CSlave *i2c)
{
    MAX11615State *s = MAX11615(i2c);
    trace_max11615_recv(s->i2c.address, s->pointer);

    max11615_read(s);

    if (s->outlen >= 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: too many bytes read", __func__);
        s->outlen = 0;
    }

    const uint8_t data =  s->outbuf[s->outlen++];

    trace_max11615_recv_return(s->i2c.address, data);
    return data;
}

static int max11615_event(I2CSlave *i2c, enum i2c_event event)
{
    MAX11615State *s = MAX11615(i2c);

    trace_max11615_event(s->i2c.address, event);

    switch (event) {
    case I2C_START_RECV:
        s->outlen = 0;
        break;
    default:
        break;
    }

    return 0;
}

static const VMStateDescription vmstate_max11615 = {
    .name = TYPE_MAX11615,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_UINT16_ARRAY(channels, MAX11615State, MAX11615_NUM_CHANNELS),
        VMSTATE_UINT8(outlen, MAX11615State),
        VMSTATE_UINT8_ARRAY(outbuf, MAX11615State, 2),
        VMSTATE_UINT8(pointer, MAX11615State),
        VMSTATE_I2C_SLAVE(i2c, MAX11615State),
        VMSTATE_END_OF_LIST()
    }
};

static void max11615_init(Object *obj)
{
    /* Nothing to do */
}

I2CSlave *max11615_init_with_values(I2CBus *bus, uint8_t address,
    const uint16_t *init_values, uint32_t init_values_size)
{
    MAX11615State *s;

    s = MAX11615(i2c_slave_new(TYPE_MAX11615, address));

    for (int i = 0; i < MAX11615_NUM_CHANNELS && i < init_values_size; i++) {

        /* arbitrary value */
        uint16_t value = 0b0000101011010010;

        if (i < init_values_size) {
            value = init_values[i];
        }
        s->channels[i] = value;
    }

    i2c_slave_realize_and_unref(I2C_SLAVE(s), bus, &error_abort);

    return I2C_SLAVE(s);
}

static void max11615_realize(DeviceState *dev, Error **errp)
{
    MAX11615State *s = MAX11615(dev);

    trace_max11615_realize(s->i2c.address);

    s->pointer = 0;
    s->outlen = 0;
}

static void max11615_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = max11615_realize;
    dc->desc = "Maxim MAX11615 12-bit ADC";
    dc->vmsd = &vmstate_max11615;
    k->event = max11615_event;
    k->recv = max11615_recv;
    k->send = max11615_send;
}

static const TypeInfo max31790_info = {
    .name = TYPE_MAX11615,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX11615State),
    .class_size = sizeof(MAX11615Class),
    .instance_init = max11615_init,
    .class_init = max11615_class_init,
};

static void max31790_register_types(void)
{
    type_register_static(&max31790_info);
}

type_init(max31790_register_types)
