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
#include "hw/sensor/max11615.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define MAX11615_NUM_CHANNELS 8

struct MAX11615State {
    I2CSlave i2c;

    /* true: single-ended mode, false: differential mode */
    bool single_ended;

    /*
     * Output data coding for the MAX11612–MAX11617 is
     * binary in unipolar mode and two’s complement in bipolar mode
     */
    bool bipolar;

    /* The MAX11613/MAX11615/MAX11617 feature a 2.048V internal reference */
    uint16_t vref;

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

static void max11615_set_outbuf(MAX11615State *s, uint16_t value)
{
    uint8_t msb = value >> 8;
    uint8_t lsb = value & 0xff;
    s->outbuf[0] = 0xf0 | (msb & 0xf);
    s->outbuf[1] = lsb;
}

static void max11615_read_single_ended(MAX11615State *s)
{
    /* Table 3. Channel Selection in Single-Ended Mode */
    /* read an ADC channel, first 4 bits must be high */
    uint16_t value = s->channels[s->pointer];
    /*
     * In single-ended mode, the MAX11612–MAX11617 always
     * operates in unipolar mode irrespective of BIP/UNI.
     */
    if (value > s->vref) {
        value = 0xfff;
    }
    max11615_set_outbuf(s, value);
}

static int16_t max11615_differential_value(MAX11615State *s)
{
    /* Table 4. Channel Selection in Differential Mode */
    size_t i1 = s->pointer;
    size_t i2 = (s->pointer % 2 == 0) ? s->pointer + 1 : s->pointer - 1;

    const int16_t ch1 = s->channels[i1];
    const int16_t ch2 = s->channels[i2];
    return ch1 - ch2;
}

static void max11615_read_differential(MAX11615State *s)
{
    const int16_t value = max11615_differential_value(s);
    uint16_t vu = value;

    /* full-scale transition: Figure 12, Figure 13 */
    if (s->bipolar) {
        const uint16_t fs = s->vref / 2;
        if (value < -fs) {
            vu = 0x800;
        }
        if (value > fs) {
            vu = 0x7ff;
        }
    } else {
        /*
         * A negative differential analog input in unipolar mode causes the
         * digital output code to be zero
         */
        if (value < 0) {
            vu = 0;
        }
    }

    max11615_set_outbuf(s, vu);
}

static void max11615_read(MAX11615State *s)
{
    if (s->single_ended) {
        max11615_read_single_ended(s);
    } else {
        max11615_read_differential(s);
    }
}

static void max11615_write_config_byte(MAX11615State *s, uint8_t data)
{
    trace_max11615_write_config(s->i2c.address, data);

    uint8_t scan_select = (data >> 5) & 0x3;

    if (scan_select != 0x3) {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented scan select\n", __func__);
    }

    uint8_t channel_select = (data >> 1) & 0xf;

    /* Table 3. Channel Selection */
    if (channel_select >= MAX11615_NUM_CHANNELS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid channel select\n",
                      __func__);
        channel_select = MAX11615_NUM_CHANNELS - 1;
    }
    s->pointer = channel_select;
    s->single_ended = data & 0x1;
}

static void max11615_write_setup_byte(MAX11615State *s, uint8_t data)
{
    trace_max11615_write_setup(s->i2c.address, data);
    /* we ignore the setup byte, not implemented */

    /* 1 = no action, 0 = resets the configuration register to default */
    const bool rst = ((data >> 1) & 0x1) == 0x0;

    if (rst) {
        s->single_ended = true;
        s->pointer = 0;
    }

    s->bipolar = ((data >> 2) & 0x1) == 0x1;

    /* Table 6. Reference Voltage */
    const uint8_t sel = (data >> 4) & 0x7;

    if (sel == 0x2 || sel == 0x3) {
        qemu_log_mask(LOG_UNIMP, "%s: unsupported: external vref\n", __func__);
    }
}

static int max11615_send(I2CSlave *i2c, uint8_t data)
{
    MAX11615State *s = MAX11615(i2c);
    const uint8_t msb = (data >> 7) & 0x1;

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

    if (s->outlen >= 2) {
        /* MAX11615 supports multichannel scan with wraparound */
        /* see datasheet page 17 */

        s->outlen = 0;

        s->pointer++;
        if (s->pointer >= MAX11615_NUM_CHANNELS) {
            s->pointer = 0;
        }
    }

    max11615_read(s);

    const uint8_t data = s->outbuf[s->outlen++];

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
    .fields =
        (const VMStateField[]){ VMSTATE_BOOL(single_ended, MAX11615State),
                                VMSTATE_BOOL(bipolar, MAX11615State),
                                VMSTATE_UINT16(vref, MAX11615State),
                                VMSTATE_UINT16_ARRAY(channels, MAX11615State,
                                                     MAX11615_NUM_CHANNELS),
                                VMSTATE_UINT8(outlen, MAX11615State),
                                VMSTATE_UINT8_ARRAY(outbuf, MAX11615State, 2),
                                VMSTATE_UINT8(pointer, MAX11615State),
                                VMSTATE_I2C_SLAVE(i2c, MAX11615State),
                                VMSTATE_END_OF_LIST() }
};

I2CSlave *max11615_init_with_values(I2CBus *bus, uint8_t address,
                                    const uint16_t *init_values,
                                    uint32_t init_values_size)
{
    MAX11615State *s;

    s = MAX11615(i2c_slave_new(TYPE_MAX11615, address));

    i2c_slave_realize_and_unref(I2C_SLAVE(s), bus, &error_abort);

    for (int i = 0; i < MAX11615_NUM_CHANNELS; i++) {
        /* arbitrary value if there is no data*/
        s->channels[i] = i < init_values_size ? init_values[i] : 0x2d2;
    }

    return I2C_SLAVE(s);
}

static void max11615_reset_enter(MAX11615State *s)
{
    trace_max11615_reset_enter(s->i2c.address);

    s->single_ended = true;
    s->bipolar = false;
    s->vref = 2048;
    s->pointer = 0;
    s->outlen = 0;

    for (int i = 0; i < MAX11615_NUM_CHANNELS; i++) {
        s->channels[i] = 0x2d2;
    }
}

static void max11615_reset(Object *obj, ResetType type)
{
    MAX11615State *s = MAX11615(obj);

    max11615_reset_enter(s);
}

static void max11615_realize(DeviceState *dev, Error **errp)
{
    MAX11615State *s = MAX11615(dev);

    trace_max11615_realize(s->i2c.address);
}

static void max11615_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = max11615_realize;
    dc->desc = "Maxim MAX11615 12-bit ADC";
    dc->vmsd = &vmstate_max11615;
    rc->phases.hold = max11615_reset;
    k->event = max11615_event;
    k->recv = max11615_recv;
    k->send = max11615_send;
}

static const TypeInfo max11615_info = {
    .name = TYPE_MAX11615,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX11615State),
    .class_size = sizeof(MAX11615Class),
    .class_init = max11615_class_init,
};

static void max11615_register_types(void)
{
    type_register_static(&max11615_info);
}

type_init(max11615_register_types)
