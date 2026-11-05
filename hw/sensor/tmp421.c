/*
 * Texas Instruments TMP421 temperature sensor.
 *
 * Copyright (c) 2016 IBM Corporation.
 *
 * Largely inspired by :
 *
 * Texas Instruments TMP105 temperature sensor.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Limitations:
 * - Conversions are instantaneous. The conversion-rate register and the
 *   shutdown bit are stored but do not decide when a measurement appears,
 *   and a one-shot write is accepted without scheduling anything: a read
 *   always returns a fresh value.
 * - The N-factor correction registers are stored and never applied. The model
 *   has no ideality factor to correct for.
 * - The sensor-fault OPEN bit and the PVLD bit of the temperature low byte are
 *   never set, so neither an open circuit nor a low supply is reported.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-type-infos.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/sensor/tmp421.h"

/* Manufacturer / Device ID's */
#define TMP421_MANUFACTURER_ID          0x55
#define TMP421_DEVICE_ID                0x21
#define TMP422_DEVICE_ID                0x22
#define TMP423_DEVICE_ID                0x23

struct TMP421State {
    /*< private >*/
    I2CSlave parent_obj;
    /*< public >*/

    /* Measured temperature, millidegrees Celsius */
    int32_t temp_mc[4];
    uint8_t n_correct[3];   /* remote channels only */

    /* migration only: the register form the stream carries */
    int16_t temperature[4];
    bool temp_mc_loaded;

    uint8_t status;
    uint8_t config[2];
    uint8_t rate;

    uint8_t len;
    uint8_t buf[2];
    uint8_t pointer;

};

struct TMP421Class {
    I2CSlaveClass parent_class;

    uint8_t device_id;
    uint8_t nchannels;      /* local + remote */
    uint8_t config2_reset;
};

OBJECT_DECLARE_TYPE(TMP421State, TMP421Class, TMP421)


/* the TMP421 registers */
#define TMP421_STATUS_REG               0x08
#define    TMP421_STATUS_BUSY             (1 << 7)
#define TMP421_CONFIG_REG_1             0x09
#define    TMP421_CONFIG_RANGE            (1 << 2)
#define    TMP421_CONFIG_SHUTDOWN         (1 << 6)
#define TMP421_CONFIG_REG_2             0x0A
#define    TMP421_CONFIG_RC               (1 << 2)
#define    TMP421_CONFIG_LEN              (1 << 3)
#define    TMP421_CONFIG_REN              (1 << 4)
#define    TMP421_CONFIG_REN2             (1 << 5)
#define    TMP421_CONFIG_REN3             (1 << 6)

#define TMP421_CONVERSION_RATE_REG      0x0B
#define TMP421_ONE_SHOT                 0x0F

#define TMP421_N_CORRECT                0x21

#define TMP421_RESET                    0xFC
#define TMP421_MANUFACTURER_ID_REG      0xFE
#define TMP421_DEVICE_ID_REG            0xFF

#define TMP421_TEMP_MSB0                0x00
#define TMP421_TEMP_MSB1                0x01
#define TMP421_TEMP_MSB2                0x02
#define TMP421_TEMP_MSB3                0x03
#define TMP421_TEMP_LSB0                0x10
#define TMP421_TEMP_LSB1                0x11
#define TMP421_TEMP_LSB2                0x12
#define TMP421_TEMP_LSB3                0x13

static const int32_t mins[2] = { -40000, -55000 };
static const int32_t maxs[2] = { 127000, 150000 };

/*
 * Register form of a measurement (datasheet Table 1): whole degrees in the
 * high byte, the fraction in the upper nibble of the low byte. The standard
 * range is two's complement and saturates at -64 C (C0h) and +127 C (7Fh);
 * the extended range is unsigned with a -64 C offset, spanning -64 C (00h) to
 * +191 C (FFh). The range only selects an encoding: the measurement itself is
 * held apart, so switching range re-reads the same temperature.
 */
#define TMP421_EXT_OFFSET   (64 * 256)
#define TMP421_STD_REG_MIN  (-64 * 256)          /* C0h, -64 C */
#define TMP421_STD_REG_MAX  (127 * 256 + 0xf0)   /* 7Fh, +127 C */
#define TMP421_EXT_REG_MAX  (255 * 256 + 0xf0)   /* FFh, +191 C offset */

static uint16_t tmp421_encode(const TMP421State *s, unsigned channel)
{
    bool ext_range = s->config[0] & TMP421_CONFIG_RANGE;
    int raw = s->temp_mc[channel] * 256 / 1000;

    if (ext_range) {
        raw = MAX(0, MIN(TMP421_EXT_REG_MAX, raw + TMP421_EXT_OFFSET));
    } else {
        raw = MAX(TMP421_STD_REG_MIN, MIN(TMP421_STD_REG_MAX, raw));
    }

    return raw;
}

/*
 * The inverse of tmp421_encode(). Only the standard range is signed: the
 * extended one spans the whole byte, so its top bit is a temperature, not a
 * sign. Rounding follows the sign so that every register value the encoder
 * can produce decodes back to the measurement it came from.
 */
static int32_t tmp421_decode(const TMP421State *s, int16_t reg)
{
    bool ext_range = s->config[0] & TMP421_CONFIG_RANGE;
    int32_t raw = ext_range ? (uint16_t)reg - TMP421_EXT_OFFSET : reg;

    return (raw * 1000 + (raw < 0 ? -128 : 128)) / 256;
}

static void tmp421_get_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    TMP421State *s = TMP421(obj);
    int64_t value;
    int tempid;

    if (sscanf(name, "temperature%d", &tempid) != 1) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    if (tempid >= 4 || tempid < 0) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    value = s->temp_mc[tempid];

    visit_type_int(v, name, &value, errp);
}

/* Units are 0.001 centigrades relative to 0 C. */
static void tmp421_set_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    TMP421State *s = TMP421(obj);
    int64_t temp;
    bool ext_range = (s->config[0] & TMP421_CONFIG_RANGE);
    int tempid;

    if (!visit_type_int(v, name, &temp, errp)) {
        return;
    }

    if (temp >= maxs[ext_range] || temp < mins[ext_range]) {
        error_setg(errp, "value %" PRId64 ".%03" PRIu64 " C is out of range",
                   temp / 1000, temp % 1000);
        return;
    }

    if (sscanf(name, "temperature%d", &tempid) != 1) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    if (tempid >= 4 || tempid < 0) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    s->temp_mc[tempid] = temp;
}

static bool tmp421_channel_enabled(TMP421State *s, unsigned channel)
{
    TMP421Class *sc = TMP421_GET_CLASS(s);

    return channel < sc->nchannels &&
           (s->config[1] & (1 << (3 + channel)));
}

/*
 * A disabled or absent channel reads back zero. The low byte carries the
 * temperature fraction in its upper nibble.
 */
static uint8_t tmp421_temp_high(TMP421State *s, unsigned channel)
{
    return tmp421_channel_enabled(s, channel) ?
           tmp421_encode(s, channel) >> 8 : 0;
}

static uint8_t tmp421_temp_low(TMP421State *s, unsigned channel)
{
    return tmp421_channel_enabled(s, channel) ?
           tmp421_encode(s, channel) & 0xf0 : 0;
}

static void tmp421_read(TMP421State *s)
{
    TMP421Class *sc = TMP421_GET_CLASS(s);

    s->len = 0;

    if (s->pointer <= TMP421_TEMP_MSB3) {
        /* high byte read latches the paired low byte from one conversion */
        unsigned channel = s->pointer - TMP421_TEMP_MSB0;
        s->buf[s->len++] = tmp421_temp_high(s, channel);
        s->buf[s->len++] = tmp421_temp_low(s, channel);
        return;
    }
    if (s->pointer >= TMP421_TEMP_LSB0 && s->pointer <= TMP421_TEMP_LSB3) {
        s->buf[s->len++] = tmp421_temp_low(s, s->pointer - TMP421_TEMP_LSB0);
        return;
    }
    if (s->pointer >= TMP421_N_CORRECT && s->pointer < TMP421_N_CORRECT + 3) {
        unsigned idx = s->pointer - TMP421_N_CORRECT;
        s->buf[s->len++] = idx + 1 < sc->nchannels ? s->n_correct[idx] : 0;
        return;
    }

    switch (s->pointer) {
    case TMP421_MANUFACTURER_ID_REG:
        s->buf[s->len++] = TMP421_MANUFACTURER_ID;
        break;
    case TMP421_DEVICE_ID_REG:
        s->buf[s->len++] = sc->device_id;
        break;
    case TMP421_CONFIG_REG_1:
        s->buf[s->len++] = s->config[0];
        break;
    case TMP421_CONFIG_REG_2:
        s->buf[s->len++] = s->config[1];
        break;
    case TMP421_CONVERSION_RATE_REG:
        s->buf[s->len++] = s->rate;
        break;
    case TMP421_STATUS_REG:
        s->buf[s->len++] = s->status;
        break;
    default:
        s->buf[s->len++] = 0;
        break;
    }
}

static void tmp421_reset(TMP421State *s);

static void tmp421_write(TMP421State *s)
{
    TMP421Class *sc = TMP421_GET_CLASS(s);

    if (s->pointer >= TMP421_N_CORRECT && s->pointer < TMP421_N_CORRECT + 3) {
        unsigned idx = s->pointer - TMP421_N_CORRECT;
        if (idx + 1 < sc->nchannels) {
            s->n_correct[idx] = s->buf[0];
        }
        return;
    }

    switch (s->pointer) {
    case TMP421_CONFIG_REG_1:
        s->config[0] = s->buf[0] &
                       (TMP421_CONFIG_RANGE | TMP421_CONFIG_SHUTDOWN);
        break;
    case TMP421_CONFIG_REG_2:
        s->config[1] = s->buf[0] & sc->config2_reset;
        break;
    case TMP421_CONVERSION_RATE_REG:
        s->rate = s->buf[0] & 0x07;
        break;
    case TMP421_ONE_SHOT:
        /* starts a conversion in shutdown; conversions are immediate here */
        break;
    case TMP421_RESET:
        tmp421_reset(s);
        break;
    }
}

static uint8_t tmp421_rx(I2CSlave *i2c)
{
    TMP421State *s = TMP421(i2c);

    if (s->len < 2) {
        return s->buf[s->len++];
    } else {
        return 0xff;
    }
}

static int tmp421_tx(I2CSlave *i2c, uint8_t data)
{
    TMP421State *s = TMP421(i2c);

    if (s->len == 0) {
        /* first byte is the register pointer for a read or write
         * operation */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        /* second byte is the data to write. The device only supports
         * one byte writes */
        s->buf[0] = data;
        tmp421_write(s);
    }

    return 0;
}

static int tmp421_event(I2CSlave *i2c, enum i2c_event event)
{
    TMP421State *s = TMP421(i2c);

    if (event == I2C_START_RECV) {
        tmp421_read(s);
    }

    s->len = 0;
    return 0;
}

static bool tmp421_n_correct_needed(void *opaque)
{
    TMP421State *s = opaque;

    return s->n_correct[0] || s->n_correct[1] || s->n_correct[2];
}

static const VMStateDescription vmstate_tmp421_n_correct = {
    .name = "TMP421/n-correct",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tmp421_n_correct_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(n_correct, TMP421State, 3),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * That form only resolves a sixteenth of a degree, so the measurement itself
 * travels in a subsection -- sent only when the register form cannot
 * reproduce it, which leaves the usual stream loadable by a QEMU that does
 * not know the subsection.
 */
static bool tmp421_temp_mc_needed(void *opaque)
{
    TMP421State *s = opaque;
    bool ext_range = s->config[0] & TMP421_CONFIG_RANGE;
    int32_t offset = ext_range ? TMP421_EXT_OFFSET : 0;

    /*
     * The register form reproduces a measurement when the conversion divides
     * exactly and the encoder had nothing to clamp. Asking the decoder
     * instead would settle the question with the function whose result the
     * subsection exists to protect, and hide any error it has.
     */
    for (unsigned channel = 0; channel < ARRAY_SIZE(s->temp_mc); channel++) {
        int32_t scaled = s->temp_mc[channel] * 256;

        if (scaled % 1000 ||
            (uint16_t)(scaled / 1000 + offset) != tmp421_encode(s, channel)) {
            return true;
        }
    }

    return false;
}

static int tmp421_temp_mc_post_load(void *opaque, int version_id)
{
    TMP421State *s = opaque;

    s->temp_mc_loaded = true;
    return 0;
}

static const VMStateDescription vmstate_tmp421_temp_mc = {
    .name = "TMP421/temperature-mc",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tmp421_temp_mc_needed,
    .post_load = tmp421_temp_mc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32_ARRAY(temp_mc, TMP421State, 4),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * The stream carries the register form of the measurement: that is what a
 * QEMU predating the millidegree state writes, and what it expects back.
 */
static int tmp421_pre_save(void *opaque)
{
    TMP421State *s = opaque;

    for (unsigned channel = 0; channel < ARRAY_SIZE(s->temp_mc); channel++) {
        s->temperature[channel] = tmp421_encode(s, channel);
    }

    return 0;
}

static int tmp421_post_load(void *opaque, int version_id)
{
    TMP421State *s = opaque;

    if (!s->temp_mc_loaded) {
        for (unsigned channel = 0; channel < ARRAY_SIZE(s->temp_mc);
             channel++) {
            s->temp_mc[channel] = tmp421_decode(s, s->temperature[channel]);
        }
    }
    s->temp_mc_loaded = false;

    return 0;
}

static const VMStateDescription vmstate_tmp421 = {
    .name = "TMP421",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = tmp421_pre_save,
    .post_load = tmp421_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(len, TMP421State),
        VMSTATE_UINT8_ARRAY(buf, TMP421State, 2),
        VMSTATE_UINT8(pointer, TMP421State),
        VMSTATE_UINT8_ARRAY(config, TMP421State, 2),
        VMSTATE_UINT8(status, TMP421State),
        VMSTATE_UINT8(rate, TMP421State),
        VMSTATE_INT16_ARRAY(temperature, TMP421State, 4),
        VMSTATE_I2C_SLAVE(parent_obj, TMP421State),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_tmp421_temp_mc,
        &vmstate_tmp421_n_correct,
        NULL
    }
};

static void tmp421_reset(TMP421State *s)
{
    TMP421Class *sc = TMP421_GET_CLASS(s);

    /*
     * The measured temperatures are not cleared: they model the physical
     * input the ADC re-samples after reset, and boards may preset them.
     */
    memset(s->n_correct, 0, sizeof(s->n_correct));
    s->pointer = 0;
    s->config[0] = 0;
    s->config[1] = sc->config2_reset;
    s->rate = 0x7;       /* 8Hz */
    s->status = 0;
}

static void tmp421_reset_hold(Object *obj, ResetType type)
{
    tmp421_reset(TMP421(obj));
}

static void tmp421_initfn(Object *obj)
{
    TMP421Class *sc = TMP421_GET_CLASS(obj);

    for (unsigned channel = 0; channel < sc->nchannels; channel++) {
        g_autofree char *name = g_strdup_printf("temperature%u", channel);

        object_property_add(obj, name, "int", &int_type_info,
                            tmp421_get_temperature,
                            tmp421_set_temperature, NULL, NULL);
        object_property_set_description(obj, name,
                            "Temperature, in millidegrees Celsius");
    }
}

static void tmp421_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TMP421Class *sc = TMP421_CLASS(klass);

    k->event = tmp421_event;
    k->recv = tmp421_rx;
    k->send = tmp421_tx;
    rc->phases.hold = tmp421_reset_hold;
    dc->vmsd = &vmstate_tmp421;

    sc->device_id = TMP421_DEVICE_ID;
    sc->config2_reset = 0x1c;
    sc->nchannels = 2;
}

static void tmp422_class_init(ObjectClass *klass, const void *data)
{
    TMP421Class *sc = TMP421_CLASS(klass);

    sc->device_id = TMP422_DEVICE_ID;
    sc->config2_reset = 0x3c;
    sc->nchannels = 3;
}

static void tmp423_class_init(ObjectClass *klass, const void *data)
{
    TMP421Class *sc = TMP421_CLASS(klass);

    sc->device_id = TMP423_DEVICE_ID;
    sc->config2_reset = 0x7c;
    sc->nchannels = 4;
}

static const TypeInfo tmp421_types[] = {
    {
        .name          = TYPE_TMP421,
        .parent        = TYPE_I2C_SLAVE,
        .instance_size = sizeof(TMP421State),
        .instance_init = tmp421_initfn,
        .class_size    = sizeof(TMP421Class),
        .class_init    = tmp421_class_init,
    },
    {
        .name          = TYPE_TMP422,
        .parent        = TYPE_TMP421,
        .class_init    = tmp422_class_init,
    },
    {
        .name          = TYPE_TMP423,
        .parent        = TYPE_TMP421,
        .class_init    = tmp423_class_init,
    },
};

DEFINE_TYPES(tmp421_types)
