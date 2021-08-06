/*
 * Remote I2C Device
 *
 * Copyright (c) 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "chardev/char-fe.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties-system.h"

#define TYPE_REMOTE_I2C "remote-i2c"
#define REMOTE_I2C(obj) OBJECT_CHECK(RemoteI2CState, (obj), TYPE_REMOTE_I2C)
#define ONE_BYTE 1

typedef struct {
    I2CSlave parent_obj;
    CharBackend chr;
} RemoteI2CState;

typedef enum {
    REMOTE_I2C_START_RECV = 0,
    REMOTE_I2C_START_SEND = 1,
    REMOTE_I2C_FINISH = 2,
    REMOTE_I2C_NACK = 3,
    REMOTE_I2C_RECV = 4,
    REMOTE_I2C_SEND = 5,
} RemoteI2CCommand;

static uint8_t remote_i2c_recv(I2CSlave *s)
{
    RemoteI2CState *i2c = REMOTE_I2C(s);
    uint8_t resp = 0;
    uint8_t type = REMOTE_I2C_RECV;
    qemu_chr_fe_write_all(&i2c->chr, &type, ONE_BYTE);

    qemu_chr_fe_read_all(&i2c->chr, &resp, ONE_BYTE);
    return resp;
}

static int remote_i2c_send(I2CSlave *s, uint8_t data)
{
    RemoteI2CState *i2c = REMOTE_I2C(s);
    uint8_t type = REMOTE_I2C_SEND;
    uint8_t resp = 1;
    qemu_chr_fe_write_all(&i2c->chr, &type, ONE_BYTE);
    qemu_chr_fe_write_all(&i2c->chr, &data, ONE_BYTE);

    qemu_chr_fe_read_all(&i2c->chr, &resp, ONE_BYTE);
    return resp ? -1 : 0;
}

/* Returns non-zero when no response from the device. */
static int remote_i2c_event(I2CSlave *s, enum i2c_event event)
{
    RemoteI2CState *i2c = REMOTE_I2C(s);
    uint8_t type;
    uint8_t resp = 1;
    switch (event) {
    case I2C_START_RECV:
        type = REMOTE_I2C_START_RECV;
        break;
    case I2C_START_SEND:
        type = REMOTE_I2C_START_SEND;
        break;
    case I2C_FINISH:
        type = REMOTE_I2C_FINISH;
        break;
    case I2C_NACK:
        type = REMOTE_I2C_NACK;
    }
    qemu_chr_fe_write_all(&i2c->chr, &type, ONE_BYTE);
    qemu_chr_fe_read_all(&i2c->chr, &resp, ONE_BYTE);
    return resp ? -1 : 0;
}

static Property remote_i2c_props[] = {
    DEFINE_PROP_CHR("chardev", RemoteI2CState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void remote_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->recv = &remote_i2c_recv;
    k->send = &remote_i2c_send;
    k->event = &remote_i2c_event;
    device_class_set_props(dc, remote_i2c_props);
}

static const TypeInfo remote_i2c_type = {
    .name = TYPE_REMOTE_I2C,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(RemoteI2CState),
    .class_size = sizeof(I2CSlaveClass),
    .class_init = remote_i2c_class_init,
};

static void remote_i2c_register(void)
{
    type_register_static(&remote_i2c_type);
}

type_init(remote_i2c_register)
