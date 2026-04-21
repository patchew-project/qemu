// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Maxim MAX31790 PMBus 6-Channel Fan Controller
 *
 * Datasheet:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31790.pdf
 *
 * Copyright(c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/sensor/max31790.h"

#define MAX31790_NUM_FANS 6
#define MAX31790_NUM_TACHS 12

#define MAX31790_REG_GLOBAL_CONFIG 0x00
#define MAX31790_REG_PWM_FREQ 0x01

/* 0x02 to 0x07: N = 0 .. 5 */
#define MAX31790_REG_FAN_CONFIG(N) (0x02 + N)

/* 0x08 to 0x0d: N = 0 .. 5 */
#define MAX31790_REG_FAN_DYNAMICS(N) (0x08 + N)

#define MAX31790_REG_FAN_FAULT_STATUS_2 0x10
#define MAX31790_REG_FAN_FAULT_STATUS_1 0x11
#define MAX31790_REG_FAN_FAULT_MASK_2 0x12
#define MAX31790_REG_FAN_FAULT_MASK_1 0x13
#define MAX31790_REG_FAILED_FAN_OPT 0x14

/* 0x18 to 0x2f: N = 0 .. 11 */
#define MAX31790_REG_TACH_COUNT_MSB(N) (0x18 + 2 * N)
#define MAX31790_REG_TACH_COUNT_LSB(N) (0x19 + 2 * N)

/* 0x30 to 0x3b: N = 0 .. 5 */
#define MAX31790_REG_PWM_DUTY_CYCLE_MSB(N) (0x30 + 2 * N)
#define MAX31790_REG_PWM_DUTY_CYCLE_LSB(N) (0x31 + 2 * N)

/* .. reserved registers ... */

/* 0x40 to 0x4b: N = 0 .. 5 */
#define MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(N) (0x40 + 2 * N)
#define MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(N) (0x41 + 2 * N)

/* ... 'User Byte' registers ... */

/* 0x50 to 0x5b: N = 0 .. 5 */
#define MAX31790_REG_TACH_TARGET_COUNT_MSB(N) (0x50 + 2 * N)
#define MAX31790_REG_TACH_TARGET_COUNT_LSB(N) (0x51 + 2 * N)

struct MAX31790State {
    I2CSlave i2c;

    uint8_t fan_config[MAX31790_NUM_FANS];
    uint8_t fan_dynamics[MAX31790_NUM_FANS];

    uint16_t pwm[MAX31790_NUM_FANS];
    uint16_t tach_target[MAX31790_NUM_FANS];
    uint16_t rpm[MAX31790_NUM_TACHS];

    /* command buffer */
    uint8_t len;
    uint8_t buf[2];

    /* output buffer */
    uint8_t outlen;
    uint8_t outbuf[2];

    /* selected register for read/write operation */
    uint8_t pointer;
};

struct MAX31790Class {
    I2CSlaveClass parent_class;
};

OBJECT_DECLARE_TYPE(MAX31790State, MAX31790Class, MAX31790)

static void max31790_read(MAX31790State *s)
{
    size_t index = 0;
    uint8_t out0 = 0;
    uint8_t out1 = 0;

    switch (s->pointer) {
    case MAX31790_REG_FAN_CONFIG(0):
    case MAX31790_REG_FAN_CONFIG(1):
    case MAX31790_REG_FAN_CONFIG(2):
    case MAX31790_REG_FAN_CONFIG(3):
    case MAX31790_REG_FAN_CONFIG(4):
    case MAX31790_REG_FAN_CONFIG(5):
        out0 = s->fan_config[s->pointer - MAX31790_REG_FAN_CONFIG(0)];
        break;
    case MAX31790_REG_FAN_DYNAMICS(0):
    case MAX31790_REG_FAN_DYNAMICS(1):
    case MAX31790_REG_FAN_DYNAMICS(2):
    case MAX31790_REG_FAN_DYNAMICS(3):
    case MAX31790_REG_FAN_DYNAMICS(4):
    case MAX31790_REG_FAN_DYNAMICS(5):
        out0 = s->fan_dynamics[s->pointer - MAX31790_REG_FAN_DYNAMICS(0)];
        break;
    case MAX31790_REG_FAN_FAULT_STATUS_1:
    case MAX31790_REG_FAN_FAULT_STATUS_2:
        /* we do not have any fan fault */
        out0 = 0x00;
        out1 = 0x00;
        break;
    case MAX31790_REG_TACH_COUNT_MSB(0):
    case MAX31790_REG_TACH_COUNT_MSB(1):
    case MAX31790_REG_TACH_COUNT_MSB(2):
    case MAX31790_REG_TACH_COUNT_MSB(3):
    case MAX31790_REG_TACH_COUNT_MSB(4):
    case MAX31790_REG_TACH_COUNT_MSB(5):
    case MAX31790_REG_TACH_COUNT_MSB(6):
    case MAX31790_REG_TACH_COUNT_MSB(7):
    case MAX31790_REG_TACH_COUNT_MSB(8):
    case MAX31790_REG_TACH_COUNT_MSB(9):
    case MAX31790_REG_TACH_COUNT_MSB(10):
    case MAX31790_REG_TACH_COUNT_MSB(11):
        index = (s->pointer - MAX31790_REG_TACH_COUNT_MSB(0)) / 2;
        out0 = (s->rpm[index] >> 8) & 0xff;
        out1 = s->rpm[index] & 0xff;
        break;

    case MAX31790_REG_TACH_COUNT_LSB(0):
    case MAX31790_REG_TACH_COUNT_LSB(1):
    case MAX31790_REG_TACH_COUNT_LSB(2):
    case MAX31790_REG_TACH_COUNT_LSB(3):
    case MAX31790_REG_TACH_COUNT_LSB(4):
    case MAX31790_REG_TACH_COUNT_LSB(5):
    case MAX31790_REG_TACH_COUNT_LSB(6):
    case MAX31790_REG_TACH_COUNT_LSB(7):
    case MAX31790_REG_TACH_COUNT_LSB(8):
    case MAX31790_REG_TACH_COUNT_LSB(9):
    case MAX31790_REG_TACH_COUNT_LSB(10):
    case MAX31790_REG_TACH_COUNT_LSB(11):
        index = (s->pointer - MAX31790_REG_TACH_COUNT_LSB(0)) / 2;
        out0 = s->rpm[index] & 0xff;
        break;

    case MAX31790_REG_PWM_DUTY_CYCLE_MSB(0):
    case MAX31790_REG_PWM_DUTY_CYCLE_MSB(1):
    case MAX31790_REG_PWM_DUTY_CYCLE_MSB(2):
    case MAX31790_REG_PWM_DUTY_CYCLE_MSB(3):
    case MAX31790_REG_PWM_DUTY_CYCLE_MSB(4):
    case MAX31790_REG_PWM_DUTY_CYCLE_MSB(5):
        index = (s->pointer - MAX31790_REG_PWM_DUTY_CYCLE_MSB(0)) / 2;
        out0 = (s->pwm[index] >> 8) & 0xff;
        out1 = s->pwm[index] & 0xff;
        break;
    case MAX31790_REG_PWM_DUTY_CYCLE_LSB(0):
    case MAX31790_REG_PWM_DUTY_CYCLE_LSB(1):
    case MAX31790_REG_PWM_DUTY_CYCLE_LSB(2):
    case MAX31790_REG_PWM_DUTY_CYCLE_LSB(3):
    case MAX31790_REG_PWM_DUTY_CYCLE_LSB(4):
    case MAX31790_REG_PWM_DUTY_CYCLE_LSB(5):
        index = (s->pointer - MAX31790_REG_PWM_DUTY_CYCLE_LSB(0)) / 2;
        out0 = s->pwm[index] & 0xff;
        break;

    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(0):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(1):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(2):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(3):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(4):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(5):
        index = (s->pointer - MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(0)) / 2;
        out0 = (s->pwm[index] >> 8) & 0xff;
        out1 = s->pwm[index] & 0xff;
        break;
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(0):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(1):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(2):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(3):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(4):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(5):
        index = (s->pointer - MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(0)) / 2;
        out0 = s->pwm[index] & 0xff;
        break;
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(0):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(1):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(2):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(3):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(4):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(5):
        index = (s->pointer - MAX31790_REG_TACH_TARGET_COUNT_MSB(0)) / 2;
        out0 = (s->tach_target[index] >> 8) & 0xff;
        out1 = s->tach_target[index] & 0xff;
        break;
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(0):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(1):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(2):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(3):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(4):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(5):
        index = (s->pointer - MAX31790_REG_TACH_TARGET_COUNT_LSB(0)) / 2;
        out0 = s->tach_target[index] & 0xff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: read of register %d", __func__,
            s->pointer);
        break;
    }

    s->outbuf[0] = out0;
    s->outbuf[1] = out1;
}

static void max31790_set_rpm(MAX31790State *s, size_t index, uint16_t rpm)
{
    /* datasheet: lowest 5 bits are 0 */
    s->rpm[index] = rpm & ~0b11111;
}

static void max31790_pwm_write(MAX31790State *s, size_t index, uint16_t value)
{
    trace_max31790_pwm_write(s->i2c.address, index, value);

    s->pwm[index] = value;

    /* change rpm based on pwm input */
    const uint16_t pwm_no_reserve = s->pwm[index] >> 7;

    /*
     * This formula has magic values which model the relationship
     * of PWM input to a fan. Not derived from datasheet.
     */
    max31790_set_rpm(s, index, 0x1000 + (pwm_no_reserve << 3));
}

static void max31790_write_2_byte(MAX31790State *s)
{
    size_t index = 0;
    const uint8_t value0 = s->buf[0];
    const uint8_t value1 = s->buf[1];
    switch (s->pointer) {
    case MAX31790_REG_FAN_CONFIG(0):
    case MAX31790_REG_FAN_CONFIG(1):
    case MAX31790_REG_FAN_CONFIG(2):
    case MAX31790_REG_FAN_CONFIG(3):
    case MAX31790_REG_FAN_CONFIG(4):
    case MAX31790_REG_FAN_CONFIG(5):
        break; /* handled by one byte write */
    case MAX31790_REG_FAN_DYNAMICS(0):
    case MAX31790_REG_FAN_DYNAMICS(1):
    case MAX31790_REG_FAN_DYNAMICS(2):
    case MAX31790_REG_FAN_DYNAMICS(3):
    case MAX31790_REG_FAN_DYNAMICS(4):
    case MAX31790_REG_FAN_DYNAMICS(5):
        break; /* handled by one byte write */
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(0):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(1):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(2):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(3):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(4):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(5):
        index = (s->pointer - MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(0)) / 2;
        max31790_pwm_write(s, index, value0 << 8 | value1);
        break;
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(0):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(1):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(2):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(3):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(4):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(5):
        index = (s->pointer - MAX31790_REG_TACH_TARGET_COUNT_MSB(0)) / 2;
        s->tach_target[index] = (value0 << 8) | value1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: write to register %d", __func__,
                s->pointer);
        break;
    }
}

static void max31790_write_1_byte(MAX31790State *s)
{

    size_t index = 0;
    uint16_t pwm = 0;
    const uint8_t value = s->buf[0];
    switch (s->pointer) {
    case MAX31790_REG_FAN_CONFIG(0):
    case MAX31790_REG_FAN_CONFIG(1):
    case MAX31790_REG_FAN_CONFIG(2):
    case MAX31790_REG_FAN_CONFIG(3):
    case MAX31790_REG_FAN_CONFIG(4):
    case MAX31790_REG_FAN_CONFIG(5):
        s->fan_config[s->pointer - MAX31790_REG_FAN_CONFIG(0)] = value;
        break;
    case MAX31790_REG_FAN_DYNAMICS(0):
    case MAX31790_REG_FAN_DYNAMICS(1):
    case MAX31790_REG_FAN_DYNAMICS(2):
    case MAX31790_REG_FAN_DYNAMICS(3):
    case MAX31790_REG_FAN_DYNAMICS(4):
    case MAX31790_REG_FAN_DYNAMICS(5):
        s->fan_dynamics[s->pointer - MAX31790_REG_FAN_DYNAMICS(0)] = value;
        break;
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(0):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(1):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(2):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(3):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(4):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(5):
        index = (s->pointer - MAX31790_REG_PWM_TARGET_DUTY_CYCLE_MSB(0)) / 2;
        pwm = (value << 8) | (s->pwm[index] & 0x00ff);
        max31790_pwm_write(s, index, pwm);
        break;
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(0):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(1):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(2):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(3):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(4):
    case MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(5):
        index = (s->pointer - MAX31790_REG_PWM_TARGET_DUTY_CYCLE_LSB(0)) / 2;
        pwm = (s->pwm[index] & 0xff00) | (value & 0x00ff);
        max31790_pwm_write(s, index, pwm);
        break;
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(0):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(1):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(2):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(3):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(4):
    case MAX31790_REG_TACH_TARGET_COUNT_MSB(5):
        index = (s->pointer - MAX31790_REG_TACH_TARGET_COUNT_MSB(0)) / 2;
        s->tach_target[index] =
            (s->tach_target[index] & 0x00ff) | (value << 8);
        break;
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(0):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(1):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(2):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(3):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(4):
    case MAX31790_REG_TACH_TARGET_COUNT_LSB(5):
        index = (s->pointer - MAX31790_REG_TACH_TARGET_COUNT_LSB(0)) / 2;
        s->tach_target[index] = (s->tach_target[index] & 0xff00) | value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: write to register %d", __func__,
            s->pointer);
        break;
    }
}

static int max31790_send(I2CSlave *i2c, uint8_t data)
{
    MAX31790State *s = MAX31790(i2c);

    trace_max31790_send(s->i2c.address, data);

    if (s->len == 0) {
        /* first byte is the register pointer for a read / write operation */
        s->pointer = data;
        s->len++;
        return 0;
    }

    if (s->len > 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write too many bytes", __func__);
        return 1; /* NAK */
    }

    /* second / third byte is the data to write */
    s->buf[s->len - 1] = data;
    s->len++;

    if (s->len == 2) {
        max31790_write_1_byte(s);
    } else if (s->len == 3) {
        max31790_write_2_byte(s);
    }

    return 0;
}

static uint8_t max31790_recv(I2CSlave *i2c)
{
    MAX31790State *s = MAX31790(i2c);
    trace_max31790_recv(s->i2c.address, s->pointer);

    max31790_read(s);
    s->len = 0;

    if (s->outlen >= 2) {
        /* error */
        s->outlen = 0;
    }

    const uint8_t data =  s->outbuf[s->outlen++];

    trace_max31790_recv_return(s->i2c.address, data);
    return data;
}

static int max31790_event(I2CSlave *i2c, enum i2c_event event)
{
    MAX31790State *s = MAX31790(i2c);

    trace_max31790_event(s->i2c.address, event);

    switch (event) {
    case I2C_START_RECV:
        s->outlen = 0;
        break;
    case I2C_START_SEND:
        s->len = 0;
        break;
    default:
        break;
    }

    return 0;
}

static const VMStateDescription vmstate_max31790 = {
    .name = TYPE_MAX31790,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_UINT8(len, MAX31790State),
        VMSTATE_UINT8_ARRAY(fan_config, MAX31790State, MAX31790_NUM_FANS),
        VMSTATE_UINT8_ARRAY(fan_dynamics, MAX31790State, MAX31790_NUM_FANS),
        VMSTATE_UINT16_ARRAY(pwm, MAX31790State, MAX31790_NUM_FANS),
        VMSTATE_UINT16_ARRAY(tach_target, MAX31790State, MAX31790_NUM_FANS),
        VMSTATE_UINT16_ARRAY(rpm, MAX31790State, MAX31790_NUM_TACHS),
        VMSTATE_UINT8_ARRAY(buf, MAX31790State, 2),
        VMSTATE_UINT8(outlen, MAX31790State),
        VMSTATE_UINT8_ARRAY(outbuf, MAX31790State, 2),
        VMSTATE_UINT8(pointer, MAX31790State),
        VMSTATE_I2C_SLAVE(i2c, MAX31790State),
        VMSTATE_END_OF_LIST()
    }
};

static void max31790_init(Object *obj)
{
    /* Nothing to do */
}

static void max31790_realize(DeviceState *dev, Error **errp)
{
    MAX31790State *s = MAX31790(dev);

    trace_max31790_realize(s->i2c.address);

    for (int i = 0; i < MAX31790_NUM_FANS; i++) {
        /* POR-State 0b 0XX0 0000 */
        s->fan_config[i] = 0b00000000;

        /* same as POR-State */
        s->tach_target[i] = 0b0011110000000000;

        /* same as POR-State */
        s->fan_dynamics[i] = 0b01001100;

        s->pwm[i] = 0;
    }

    for (int i = 0; i < MAX31790_NUM_TACHS; i++) {
        max31790_set_rpm(s, i, 0x4444);
    }
}

static void max31790_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = max31790_realize;
    dc->desc = "Maxim MAX31790 6-Channel Fan Controller";
    dc->vmsd = &vmstate_max31790;
    k->event = max31790_event;
    k->recv = max31790_recv;
    k->send = max31790_send;
}

static const TypeInfo max31790_info = {
    .name = TYPE_MAX31790,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX31790State),
    .class_size = sizeof(MAX31790Class),
    .instance_init = max31790_init,
    .class_init = max31790_class_init,
};

static void max31790_register_types(void)
{
    type_register_static(&max31790_info);
}

type_init(max31790_register_types)
