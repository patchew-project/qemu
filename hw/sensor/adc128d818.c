// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Texas Instruments ADC128D818 12 bit ADC with temperature sensor
 * Models ADC128D818
 *
 * Product:
 * https://www.ti.com/product/ADC128D818
 * Datasheet:
 * https://www.ti.com/lit/gpn/adc128d818
 *
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/sensor/adc128d818.h"

/* 8 bit, r/w */
#define REG_CONFIG 0x00

/* 8 bit, readonly */
#define REG_INTERRUPT_STATUS 0x01

/* 8 bit, r/w */
#define REG_INTERRUPT_MASK 0x03

/* 8 bit, r/w */
#define REG_CONVERSION_RATE 0x07

/* 8 bit, r/w */
#define REG_CHANNEL_DISABLE 0x08

/* 8 bit, write-only */
#define REG_ONE_SHOT 0x09

/* 8 bit, r/w */
#define REG_DEEP_SHUTDOWN 0x0a

/* 8 bit, r/w */
#define REG_ADVANCED_CONFIG 0x0b

/* 8 bit, readonly */
#define REG_BUSY_STATUS 0x0c

/* 16 bit registers, N = 0..7, readonly */
#define REG_CHANNEL_READING(N) (0x20 + N)

/* 8 bit registers N = 0..15, r/w */
#define REG_LIMIT(N) (0x2a + N)

/* 8 bit register, readonly */
#define REG_MANUFACTURER_ID 0x3e

/* 8 bit register, readonly */
#define REG_REVISION_ID 0x3f

#define ADC128D818_NUM_CHANNELS 8

struct ADC128D818State {
    I2CSlave i2c;

    uint8_t config;
    uint8_t interrupt_mask;
    uint8_t conversion_rate;
    uint8_t channel_disable;
    bool deep_shutdown;
    uint8_t advanced_config;

    /* channel reading registers, 2 bytes each */
    uint16_t channels[ADC128D818_NUM_CHANNELS];

    /* high and low limit registers 0x2a - 0x39, one byte each */
    uint8_t limit[ADC128D818_NUM_CHANNELS * 2];

    /* input buffer */
    uint8_t len;
    uint8_t buf[2];

    /* output buffer */
    uint8_t outlen;
    uint8_t outbuf[2];

    /* selected channel for read/write operation */
    uint8_t pointer;
};

struct ADC128D818Class {
    I2CSlaveClass parent_class;
};

OBJECT_DECLARE_TYPE(ADC128D818State, ADC128D818Class, ADC128D818)

static void adc128d818_read(ADC128D818State *s)
{
    uint8_t ch_num = 0;
    switch (s->pointer) {
    case REG_CONFIG:
        s->outbuf[0] = s->config;
        break;
    case REG_INTERRUPT_STATUS:
        s->outbuf[0] = 0x0; /* POR State */
        break;
    case REG_INTERRUPT_MASK:
        s->outbuf[0] = s->interrupt_mask;
        break;
    case REG_CONVERSION_RATE:
        s->outbuf[0] = s->conversion_rate;
        break;
    case REG_CHANNEL_DISABLE:
        s->outbuf[0] = s->channel_disable;
        break;
    case REG_ONE_SHOT:
        /* not marked as readable */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read of register 0x%02x\n",
            __func__, s->pointer);
        s->outbuf[0] = 0x0;
        break;
    case REG_DEEP_SHUTDOWN:
        s->outbuf[0] = s->deep_shutdown ? 0x1 : 0x0;
        break;
    case REG_ADVANCED_CONFIG:
        s->outbuf[0] = s->advanced_config & 0b111;
        break;
    case REG_BUSY_STATUS:
        /* not implemented */
        s->outbuf[0] = 0b00000010; /* POR State */
        break;
    case REG_CHANNEL_READING(0):
    case REG_CHANNEL_READING(1):
    case REG_CHANNEL_READING(2):
    case REG_CHANNEL_READING(3):
    case REG_CHANNEL_READING(4):
    case REG_CHANNEL_READING(5):
    case REG_CHANNEL_READING(6):
    case REG_CHANNEL_READING(7):
        ch_num = s->pointer - REG_CHANNEL_READING(0);
        /* high byte comes first, driver reads swapped */
        s->outbuf[0] = (s->channels[ch_num] >> 8) & 0xff;
        s->outbuf[1] = s->channels[ch_num] & 0xff;
        break;
    case REG_LIMIT(0):
    case REG_LIMIT(1):
    case REG_LIMIT(2):
    case REG_LIMIT(3):
    case REG_LIMIT(4):
    case REG_LIMIT(5):
    case REG_LIMIT(6):
    case REG_LIMIT(7):
    case REG_LIMIT(8):
    case REG_LIMIT(9):
    case REG_LIMIT(10):
    case REG_LIMIT(11):
    case REG_LIMIT(12):
    case REG_LIMIT(13):
    case REG_LIMIT(14):
    case REG_LIMIT(15):
        s->outbuf[0] = s->limit[s->pointer - REG_LIMIT(0)];
        break;
    case REG_MANUFACTURER_ID:
        s->outbuf[0] = 0x1; /* readonly */
        break;
    case REG_REVISION_ID:
        s->outbuf[0] = 0b00001001; /* readonly */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read of register 0x%02x\n",
            __func__, s->pointer);
        break;
    }
}

static void adc128d818_write_advanced_config(ADC128D818State *s, uint8_t data)
{
    /*
     * Note: Whenever the Advanced Configuration Register is programmed,
     * all of the values in the Channel Reading Registers and
     * Interrupt Status Registers will return to their default values.
     */

    s->advanced_config = (data & 0b111);
}

static void adc128d818_write(ADC128D818State *s, uint8_t data)
{
    trace_adc128d818_write(s->i2c.address, s->pointer, data);

    /* which bits in config register are writable */
    const uint8_t config_w_mask = 0b10001011;
    const uint8_t config_ro_mask = (uint8_t)~config_w_mask;

    switch (s->pointer) {
    case REG_CONFIG:
        s->config = (s->config & config_ro_mask) | (data & config_w_mask);
        break;
    case REG_INTERRUPT_MASK:
        s->interrupt_mask = data;
        break;
    case REG_CONVERSION_RATE:
        s->conversion_rate = data;
        break;
    case REG_CHANNEL_DISABLE:
        s->channel_disable = data;
        break;
    case REG_ONE_SHOT:
        /*
         * Initiate a single conversion and comparison cycle when
         * the device is in shutdown mode or deep shutdown mode, after
         * which the device returns to the respective mode that it was in
         *
         */
        break;
    case REG_DEEP_SHUTDOWN:
        s->deep_shutdown = (data & 0x1) != 0;
        break;
    case REG_ADVANCED_CONFIG:
        adc128d818_write_advanced_config(s, data);
        break;
    case REG_LIMIT(0):
    case REG_LIMIT(1):
    case REG_LIMIT(2):
    case REG_LIMIT(3):
    case REG_LIMIT(4):
    case REG_LIMIT(5):
    case REG_LIMIT(6):
    case REG_LIMIT(7):
    case REG_LIMIT(8):
    case REG_LIMIT(9):
    case REG_LIMIT(10):
    case REG_LIMIT(11):
    case REG_LIMIT(12):
    case REG_LIMIT(13):
    case REG_LIMIT(14):
    case REG_LIMIT(15):
        s->limit[s->pointer - REG_LIMIT(0)] = data;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write of register 0x%02x\n",
            __func__, s->pointer);
        break;
    }
}

static int adc128d818_send(I2CSlave *i2c, uint8_t data)
{
    ADC128D818State *s = ADC128D818(i2c);
    trace_adc128d818_send(s->i2c.address, data);

    s->outlen = 0;
    s->buf[s->len] = data;

    if (s->len == 0) {
        s->pointer = data;
    } else if (s->len == 1) {
        adc128d818_write(s, data);
    }

    s->len++;
    return 0;
}

static uint8_t adc128d818_recv(I2CSlave *i2c)
{
    ADC128D818State *s = ADC128D818(i2c);
    trace_adc128d818_recv(s->i2c.address, s->pointer);

    adc128d818_read(s);

    if (s->outlen >= 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: too many bytes read\n", __func__);
        s->outlen = 0;
    }

    const uint8_t data =  s->outbuf[s->outlen++];

    trace_adc128d818_recv_return(s->i2c.address, data);
    return data;
}

static int adc128d818_event(I2CSlave *i2c, enum i2c_event event)
{
    ADC128D818State *s = ADC128D818(i2c);

    trace_adc128d818_event(s->i2c.address, event);

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

static const VMStateDescription vmstate_adc128d818 = {
    .name = TYPE_ADC128D818,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_UINT8(config, ADC128D818State),
        VMSTATE_UINT8(interrupt_mask, ADC128D818State),
        VMSTATE_UINT8(conversion_rate, ADC128D818State),
        VMSTATE_UINT8(channel_disable, ADC128D818State),
        VMSTATE_BOOL(deep_shutdown, ADC128D818State),
        VMSTATE_UINT8(advanced_config, ADC128D818State),
        VMSTATE_UINT16_ARRAY(channels, ADC128D818State,
            ADC128D818_NUM_CHANNELS),
        VMSTATE_UINT8_ARRAY(limit, ADC128D818State,
            ADC128D818_NUM_CHANNELS * 2),
        VMSTATE_UINT8(len, ADC128D818State),
        VMSTATE_UINT8_ARRAY(buf, ADC128D818State, 2),
        VMSTATE_UINT8(outlen, ADC128D818State),
        VMSTATE_UINT8_ARRAY(outbuf, ADC128D818State, 2),
        VMSTATE_UINT8(pointer, ADC128D818State),
        VMSTATE_I2C_SLAVE(i2c, ADC128D818State),
        VMSTATE_END_OF_LIST()
    }
};

static void adc128d818_init(Object *obj)
{
    /* Nothing to do */
}

I2CSlave *adc128d818_init_with_values(I2CBus *bus, uint8_t address,
     const uint16_t *init_values, uint32_t init_values_size)
{
    ADC128D818State *s;

    s = ADC128D818(i2c_slave_new(TYPE_ADC128D818, address));

    for (int i = 0; i < ADC128D818_NUM_CHANNELS; i++) {

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

static void adc128d818_reset(I2CSlave *i2c)
{
    ADC128D818State *s = ADC128D818(i2c);

    s->pointer = 0;
    s->outlen = 0;

    /* POR-State */
    s->config = 0b00001000;
    s->interrupt_mask = 0;
    s->conversion_rate = 0;
    s->channel_disable = 0;
    s->deep_shutdown = 0;
    s->advanced_config = 0;

    /* No POR-State defined in datasheet */
    for (int i = 0; i < ADC128D818_NUM_CHANNELS * 2; i++) {
        s->limit[i] = 0;
    }
}

static void adc128d818_realize(DeviceState *dev, Error **errp)
{
    ADC128D818State *s = ADC128D818(dev);

    trace_adc128d818_realize(s->i2c.address);

    adc128d818_reset(&s->i2c);
}

static void adc128d818_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = adc128d818_realize;
    dc->desc = "Texas Intstruments ADC128D818 12-bit ADC with temp sensor";
    dc->vmsd = &vmstate_adc128d818;
    k->event = adc128d818_event;
    k->recv = adc128d818_recv;
    k->send = adc128d818_send;
}

static const TypeInfo adc128d818_info = {
    .name = TYPE_ADC128D818,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(ADC128D818State),
    .class_size = sizeof(ADC128D818Class),
    .instance_init = adc128d818_init,
    .class_init = adc128d818_class_init,
};

static void adc128d818_register_types(void)
{
    type_register_static(&adc128d818_info);
}

type_init(adc128d818_register_types)
