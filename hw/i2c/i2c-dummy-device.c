/*
 * QEMU I2C Dummy Device
 *
 * Copyright (c) 2026 Timur Maksimov <gessingate@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_I2C_DUMMY_DEVICE "i2c-dummy-device"
OBJECT_DECLARE_SIMPLE_TYPE(I2CDummyDeviceState, I2C_DUMMY_DEVICE)

#define I2C_DUMMY_ARRAY_SIZE 64

struct I2CDummyDeviceState {
    I2CSlave parent_obj;

    uint8_t tx[I2C_DUMMY_ARRAY_SIZE];
    uint8_t rx[I2C_DUMMY_ARRAY_SIZE];

    uint8_t tx_pos;
    uint8_t rx_pos;
    uint8_t event;
};

static void i2c_dummy_device_reset(DeviceState *dev)
{
    I2CDummyDeviceState *s = I2C_DUMMY_DEVICE(dev);

    s->tx_pos = 0;
    s->rx_pos = 0;
    s->event = I2C_FINISH;
    memset(s->tx, 0, sizeof s->tx);
    /* Don't reset rx as it is a readonly parameter for the model */
}

static int i2c_dummy_device_event(I2CSlave *i2c, enum i2c_event event)
{
    I2CDummyDeviceState *s = I2C_DUMMY_DEVICE(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->tx_pos = 0;
        break;

    case I2C_START_RECV:
        s->rx_pos = 0;
        break;

    default:
        break;
    }

    s->event = event;
    return 0;
}

static int i2c_dummy_device_send(I2CSlave *i2c, uint8_t data)
{
    I2CDummyDeviceState *s = I2C_DUMMY_DEVICE(i2c);

    if (s->tx_pos >= I2C_DUMMY_ARRAY_SIZE) {
        /*
         * The TX buffer is full. NACK the byte.
         */
        return 1;
    }

    if (s->event == I2C_START_SEND) {
        s->tx[s->tx_pos++] = data;
    }

    return 0;
}

static uint8_t i2c_dummy_device_recv(I2CSlave *i2c)
{
    I2CDummyDeviceState *s = I2C_DUMMY_DEVICE(i2c);

    if (s->rx_pos >= I2C_DUMMY_ARRAY_SIZE || s->event != I2C_START_RECV) {
        /*
         * There is no more data available.
         */
        return 0xff;
    }

    return s->rx[s->rx_pos++];
}

static void i2c_dummy_device_init(Object *obj)
{
    I2CDummyDeviceState *s = I2C_DUMMY_DEVICE(obj);

    for (int i = 0; i < I2C_DUMMY_ARRAY_SIZE; i++) {
        g_autofree char *name = g_strdup_printf("tx[%d]", i);

        object_property_add_uint8_ptr(
            obj, name, &s->tx[i],
            OBJ_PROP_FLAG_READWRITE);
    }

    for (int i = 0; i < I2C_DUMMY_ARRAY_SIZE; i++) {
        g_autofree char *name = g_strdup_printf("rx[%d]", i);

        object_property_add_uint8_ptr(
            obj, name, &s->rx[i],
            OBJ_PROP_FLAG_READWRITE);
    }

    object_property_add_uint8_ptr(
            obj, "tx-pos", &s->tx_pos,
            OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint8_ptr(
            obj, "rx-pos", &s->rx_pos,
            OBJ_PROP_FLAG_READWRITE);
}

static const VMStateDescription vmstate_i2c_dummy_device = {
    .name = TYPE_I2C_DUMMY_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,

    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, I2CDummyDeviceState),

        VMSTATE_UINT8_ARRAY(tx, I2CDummyDeviceState,
                            I2C_DUMMY_ARRAY_SIZE),

        VMSTATE_UINT8_ARRAY(rx, I2CDummyDeviceState,
                            I2C_DUMMY_ARRAY_SIZE),

        VMSTATE_UINT8(tx_pos, I2CDummyDeviceState),
        VMSTATE_UINT8(rx_pos, I2CDummyDeviceState),
        VMSTATE_UINT8(event, I2CDummyDeviceState),

        VMSTATE_END_OF_LIST()
    },
};

static void i2c_dummy_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    device_class_set_legacy_reset(dc, i2c_dummy_device_reset);

    dc->vmsd = &vmstate_i2c_dummy_device;

    isc->event = i2c_dummy_device_event;
    isc->send = i2c_dummy_device_send;
    isc->recv = i2c_dummy_device_recv;
}

static const TypeInfo i2c_dummy_device_info[] = {
    {
        .name = TYPE_I2C_DUMMY_DEVICE,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(I2CDummyDeviceState),
        .instance_init = i2c_dummy_device_init,
        .class_init = i2c_dummy_device_class_init,
    }
};

DEFINE_TYPES(i2c_dummy_device_info)
