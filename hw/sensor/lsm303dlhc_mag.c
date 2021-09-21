/*
 * LSM303DLHC I2C magnetometer.
 *
 * Copyright (C) 2021 Linaro Ltd.
 * Written by Kevin Townsend <kevin.townsend@linaro.org>
 *
 * Based on: https://www.st.com/resource/en/datasheet/lsm303dlhc.pdf
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The I2C address associated with this device is set on the command-line when
 * initialising the machine, but the following address is standard: 0x1E.
 * 
 * Get and set functions for 'mag-x', 'mag-y' and 'mag-z' assume that
 * 1 = 0.001 uT. (NOTE the 1 gauss = 100 uT, so setting a value of 100,000
 * would be equal to 1 gauss or 100 uT.)
 * 
 * Get and set functions for 'temperature' assume that 1 = 0.001 C, so 23.6 C
 * would be equal to 23600.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/bswap.h"

enum LSM303DLHCMagReg {
    LSM303DLHC_MAG_REG_CRA          = 0x00,
    LSM303DLHC_MAG_REG_CRB          = 0x01,
    LSM303DLHC_MAG_REG_MR           = 0x02,
    LSM303DLHC_MAG_REG_OUT_X_H      = 0x03,
    LSM303DLHC_MAG_REG_OUT_X_L      = 0x04,
    LSM303DLHC_MAG_REG_OUT_Z_H      = 0x05,
    LSM303DLHC_MAG_REG_OUT_Z_L      = 0x06,
    LSM303DLHC_MAG_REG_OUT_Y_H      = 0x07,
    LSM303DLHC_MAG_REG_OUT_Y_L      = 0x08,
    LSM303DLHC_MAG_REG_SR           = 0x09,
    LSM303DLHC_MAG_REG_IRA          = 0x0A,
    LSM303DLHC_MAG_REG_IRB          = 0x0B,
    LSM303DLHC_MAG_REG_IRC          = 0x0C,
    LSM303DLHC_MAG_REG_TEMP_OUT_H   = 0x31,
    LSM303DLHC_MAG_REG_TEMP_OUT_L   = 0x32
};

typedef struct LSM303DLHCMagState {
    I2CSlave parent_obj;
    uint8_t cra;
    uint8_t crb;
    uint8_t mr;
    int16_t x;
    int16_t z;
    int16_t y;
    uint8_t sr;
    uint8_t ira;
    uint8_t irb;
    uint8_t irc;
    int16_t temperature;
    uint8_t len;
    uint8_t buf[6];
    uint8_t pointer;
} LSM303DLHCMagState;

#define TYPE_LSM303DLHC_MAG "lsm303dlhc_mag"
OBJECT_DECLARE_SIMPLE_TYPE(LSM303DLHCMagState, LSM303DLHC_MAG)

static void lsm303dlhc_mag_get_x(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value = s->x;

    /* Convert to uT where 1000 = 1 uT. Conversion factor depends on gain. */
    value *= 1000000;
    switch (s->crb >> 5) {
        case 1:
            /* 11 lsb per uT. */
            value /= 11000;
            break;
        case 2:
            /* 8.55 lsb per uT. */
            value /= 8550;
            break;
        case 3:
            /* 6.70 lsb per uT. */
            value /= 6700;
            break;
        case 4:
            /* 4.50 lsb per uT. */
            value /= 4500;
            break;
        case 5:
            /* 4.00 lsb per uT. */
            value /= 4000;
            break;
        case 6:
            /* 3.30 lsb per uT. */
            value /= 3300;
            break;
        case 7:
            /* 2.30 lsb per uT. */
            value /= 2300;
            break;
        default:
            break;
    }

    visit_type_int(v, name, &value, errp);
}

static void lsm303dlhc_mag_get_y(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value = s->y;

    /* Convert to uT where 1000 = 1 uT. Conversion factor depends on gain. */
    value *= 1000000;
    switch (s->crb >> 5) {
        case 1:
            /* 11 lsb per uT. */
            value /= 11000;
            break;
        case 2:
            /* 8.55 lsb per uT. */
            value /= 8550;
            break;
        case 3:
            /* 6.70 lsb per uT. */
            value /= 6700;
            break;
        case 4:
            /* 4.50 lsb per uT. */
            value /= 4500;
            break;
        case 5:
            /* 4.00 lsb per uT. */
            value /= 4000;
            break;
        case 6:
            /* 3.30 lsb per uT. */
            value /= 3300;
            break;
        case 7:
            /* 2.30 lsb per uT. */
            value /= 2300;
            break;
        default:
            break;
    }

    visit_type_int(v, name, &value, errp);
}

static void lsm303dlhc_mag_get_z(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value = s->z;

    /* Convert to uT where 1000 = 1 uT. Conversion factor depends on gain. */
    value *= 1000000;
    switch (s->crb >> 5) {
        case 1:
            /* 9.8 lsb per uT. */
            value /= 9800;
            break;
        case 2:
            /* 7.6 lsb per uT. */
            value /= 7600;
            break;
        case 3:
            /* 6.0 lsb per uT. */
            value /= 6000;
            break;
        case 4:
            /* 4.0 lsb per uT. */
            value /= 4000;
            break;
        case 5:
            /* 3.55 lsb per uT. */
            value /= 3550;
            break;
        case 6:
            /* 2.95 lsb per uT. */
            value /= 2950;
            break;
        case 7:
            /* 2.05 lsb per uT. */
            value /= 2050;
            break;
        default:
            break;
    }

    visit_type_int(v, name, &value, errp);
}

static void lsm303dlhc_mag_set_x(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;
    int64_t reg;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    /* Avoid divide by zero errors on valid zero value. */
    if (value == 0) {
        s->x = 0;
        return;
    }

    /* Convert input from uT, accounting for current gain settings. */
    switch (s->crb >> 5) {
        case 1:
            /* 11 lsb per uT = 0.0909090909 uT per lsb. */
            reg = value * 1000 / 90909;
            break;
        case 2:
            /* 8.55 lsb per uT = 0.1169590643 uT per lsb. */
            reg = value * 1000 / 116959;
            break;
        case 3:
            /* 6.7 lsb per uT = 0.1492537313 uT per lsb. */
            reg = value * 1000 / 149253;
            break;
        case 4:
            /* 4.5 lsb per uT = 0.2222222222 uT per lsb */
            reg = value * 1000 / 222222;
            break;
        case 5:
            /* 4.0 lsb per uT = 0.25 uT per lsb. */
            reg = value * 1000 / 250000;
            break;
        case 6:
            /* 3.3 lsb per uT = 0.303030303 uT per lsb */
            reg = value * 1000 / 303030;
            break;
        case 7:
            /* 2.3 lsb per uT = 0.4347826087 uT per lsb */
            reg = value * 1000 / 434782;
            break;
        default:
            error_setg(errp, "invalid gain in crb: 0x%02X", s->crb);
            return;
    }

    /* Make sure we are within a 12-bit limit. */
    if (reg > 2047 || reg < -2048) {
        error_setg(errp, "value %lld out of register's range", value);
        return;
    }

    s->x = (int16_t)reg;
}

static void lsm303dlhc_mag_set_y(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;
    int64_t reg;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    /* Avoid divide by zero errors on valid zero value. */
    if (value == 0) {
        s->y = 0;
        return;
    }

    /* Convert input to Gauss, accounting for current gain settings. */
    switch (s->crb >> 5) {
        case 1:
            /* 11 lsb per uT = 0.0909090909 uT per lsb. */
            reg = value * 1000 / 90909;
            break;
        case 2:
            /* 8.55 lsb per uT = 0.1169590643 uT per lsb. */
            reg = value * 1000 / 116959;
            break;
        case 3:
            /* 6.7 lsb per uT = 0.1492537313 uT per lsb. */
            reg = value * 1000 / 149253;
            break;
        case 4:
            /* 4.5 lsb per uT = 0.2222222222 uT per lsb */
            reg = value * 1000 / 222222;
            break;
        case 5:
            /* 4.0 lsb per uT = 0.25 uT per lsb. */
            reg = value * 1000 / 250000;
            break;
        case 6:
            /* 3.3 lsb per uT = 0.303030303 uT per lsb */
            reg = value * 1000 / 303030;
            break;
        case 7:
            /* 2.3 lsb per uT = 0.4347826087 uT per lsb */
            reg = value * 1000 / 434782;
            break;
        default:
            error_setg(errp, "invalid gain in crb: 0x%02X", s->crb);
            return;
    }

    /* Make sure we are within a 12-bit limit. */
    if (reg > 2047 || reg < -2048) {
        error_setg(errp, "value %lld out of register's range", value);
        return;
    }

    s->y = (int16_t)reg;
}
static void lsm303dlhc_mag_set_z(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;
    int64_t reg;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    /* Avoid divide by zero errors on valid zero value. */
    if (value == 0) {
        s->z = 0;
        return;
    }

    /* Convert input to Gauss, accounting for current gain settings. */
    switch (s->crb >> 5) {
        case 1:
            /* 9.8 lsb per uT = 0.1020408163 uT per lsb. */
            reg = value * 1000 / 102040;
            break;
        case 2:
            /* 7.6 lsb per uT = 0.1315789474 uT per lsb. */
            reg = value * 1000 / 131578;
            break;
        case 3:
            /* 6.0 lsb per uT = 0.1666666667 uT per lsb. */
            reg = value * 1000 / 166666;
            break;
        case 4:
            /* 4.0 lsb per uT = 0.25 uT per lsb */
            reg = value * 1000 / 250000;
            break;
        case 5:
            /* 3.55 lsb per uT = 0.2816901408 uT per lsb. */
            reg = value * 1000 / 281690;
            break;
        case 6:
            /* 2.95 lsb per uT = 0.3389830508 uT per lsb */
            reg = value * 1000 / 338983;
            break;
        case 7:
            /* 2.05 lsb per uT = 0.487804878 uT per lsb */
            reg = value * 1000 / 487804;
            break;
        default:
            error_setg(errp, "invalid gain in crb: 0x%02X", s->crb);
            return;
    }

    /* Make sure we are within a 12-bit limit. */
    if (reg > 2047 || reg < -2048) {
        error_setg(errp, "value %lld out of register's range", value);
        return;
    }

    s->z = (int16_t)reg;
}

/*
 * Get handler for the temperature property.
 */
static void lsm303dlhc_mag_get_temperature(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;

    /* Convert to 1 lsb = 0.125 C to 1 = 0.001 C for 'temperature' property. */
    value = s->temperature * 125;

    visit_type_int(v, name, &value, errp);
}

/*
 * Set handler for the temperature property.
 */
static void lsm303dlhc_mag_set_temperature(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    /* Input temperature is in 0.001 C units. Convert to 1 lsb = 0.125 C. */
    value /= 125;

    if (value > 2047 || value < -2048) {
        error_setg(errp, "value %lld lsb is out of range", value);
        return;
    }

    s->temperature = (int16_t)value;
}

/*
 * Callback handler whenever a 'I2C_START_RECV' (read) event is received.
 */
static void lsm303dlhc_mag_read(LSM303DLHCMagState *s)
{
    s->len = 0;

    /*
     * The address pointer on the LSM303DLHC auto-increments whenever a byte
     * is read, without the master device having to request the next address.
     *
     * The auto-increment process has the following logic:
     *
     *   - if (s->pointer == 8) then s->pointer = 3
     *   - else: if (s->pointer >= 12) then s->pointer = 0
     *   - else: s->pointer += 1
     *
     * Reading an invalid address return 0.
     *
     * The auto-increment logic is only taken into account in this driver
     * for the LSM303DLHC_MAG_REG_OUT_* and LSM303DLHC_MAG_REG_TEMP_OUT_*
     * registers, which are the two common uses cases for it. Accessing either
     * of these register sets will also populate the rest of the related
     * dataset.
     */

    switch (s->pointer) {
    case LSM303DLHC_MAG_REG_CRA:
        s->buf[s->len++] = s->cra;
        break;
    case LSM303DLHC_MAG_REG_CRB:
        s->buf[s->len++] = s->crb;
        break;
    case LSM303DLHC_MAG_REG_MR:
        s->buf[s->len++] = s->mr;
        break;
    case LSM303DLHC_MAG_REG_OUT_X_H:
        stw_be_p(s->buf, s->x);
        s->len += sizeof(s->x);
        stw_be_p(s->buf + 2, s->z);
        s->len += sizeof(s->z);
        stw_be_p(s->buf + 4, s->y);
        s->len += sizeof(s->y);
        break;
    case LSM303DLHC_MAG_REG_OUT_X_L:
        s->buf[s->len++] = (uint8_t)(s->x);
        stw_be_p(s->buf + 1, s->z);
        s->len += sizeof(s->z);
        stw_be_p(s->buf + 3, s->y);
        s->len += sizeof(s->y);
        s->buf[s->len++] = (uint8_t)(s->x >> 8);
        break;
    case LSM303DLHC_MAG_REG_OUT_Z_H:
        stw_be_p(s->buf, s->z);
        s->len += sizeof(s->z);
        stw_be_p(s->buf + 2, s->y);
        s->len += sizeof(s->y);
        stw_be_p(s->buf + 4, s->x);
        s->len += sizeof(s->x);
        break;
    case LSM303DLHC_MAG_REG_OUT_Z_L:
        s->buf[s->len++] = (uint8_t)(s->z);
        stw_be_p(s->buf + 1, s->y);
        s->len += sizeof(s->y);
        stw_be_p(s->buf + 3, s->x);
        s->len += sizeof(s->x);
        s->buf[s->len++] = (uint8_t)(s->z >> 8);
        break;
    case LSM303DLHC_MAG_REG_OUT_Y_H:
        stw_be_p(s->buf, s->y);
        s->len += sizeof(s->y);
        stw_be_p(s->buf + 2, s->x);
        s->len += sizeof(s->x);
        stw_be_p(s->buf + 4, s->z);
        s->len += sizeof(s->z);
        break;
    case LSM303DLHC_MAG_REG_OUT_Y_L:
        s->buf[s->len++] = (uint8_t)(s->y);
        stw_be_p(s->buf + 1, s->x);
        s->len += sizeof(s->x);
        stw_be_p(s->buf + 3, s->z);
        s->len += sizeof(s->z);
        s->buf[s->len++] = (uint8_t)(s->y >> 8);
        break;
    case LSM303DLHC_MAG_REG_SR:
        s->buf[s->len++] = s->sr;
        break;
    case LSM303DLHC_MAG_REG_IRA:
        s->buf[s->len++] = s->ira;
        break;
    case LSM303DLHC_MAG_REG_IRB:
        s->buf[s->len++] = s->irb;
        break;
    case LSM303DLHC_MAG_REG_IRC:
        s->buf[s->len++] = s->irc;
        break;
    case LSM303DLHC_MAG_REG_TEMP_OUT_H:
        /* Check if the temperature sensor is enabled or not (CRA & 0x80). */
        if (s->cra & 0x80) {
            s->buf[s->len++] = (uint8_t)(s->temperature >> 8);
            s->buf[s->len++] = (uint8_t)(s->temperature & 0xf0);
        } else {
            s->buf[s->len++] = 0;
            s->buf[s->len++] = 0;
        }
        break;
    case LSM303DLHC_MAG_REG_TEMP_OUT_L:
        if (s->cra & 0x80) {
            s->buf[s->len++] = (uint8_t)(s->temperature & 0xf0);
        } else {
            s->buf[s->len++] = 0;
        }
        break;
    default:
        s->buf[s->len++] = 0;
        break;
    }
}

/*
 * Callback handler when a device attempts to write to a register.
 */
static void lsm303dlhc_mag_write(LSM303DLHCMagState *s)
{
    switch (s->pointer) {
    case LSM303DLHC_MAG_REG_CRA:
        s->cra = s->buf[0];
        break;
    case LSM303DLHC_MAG_REG_CRB:
        /* Make sure gain is at least 1, falling back to 1 on an error. */
        if (s->buf[0] >> 5 == 0) {
            s->buf[0] = 1 << 5;
        }
        s->crb = s->buf[0];
        break;
    case LSM303DLHC_MAG_REG_MR:
        s->mr = s->buf[0];
        break;
    case LSM303DLHC_MAG_REG_SR:
        s->sr = s->buf[0];
        break;
    case LSM303DLHC_MAG_REG_IRA:
        s->ira = s->buf[0];
        break;
    case LSM303DLHC_MAG_REG_IRB:
        s->irb = s->buf[0];
        break;
    case LSM303DLHC_MAG_REG_IRC:
        s->irc = s->buf[0];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "reg is read-only: 0x%02X", s->buf[0]);
        break;
    }
}

/*
 * Low-level slave-to-master transaction handler.
 */
static uint8_t lsm303dlhc_mag_recv(I2CSlave *i2c)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    if (s->len < 6) {
        return s->buf[s->len++];
    } else {
        return 0xff;
    }
}

/*
 * Low-level master-to-slave transaction handler.
 */
static int lsm303dlhc_mag_send(I2CSlave *i2c, uint8_t data)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    if (s->len == 0) {
        /* First byte is the reg pointer */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        /* Second byte is the new register value. */
        s->buf[0] = data;
        lsm303dlhc_mag_write(s);
    } else {
        g_assert_not_reached();
    }

    return 0;
}

/*
 * Bus state change handler.
 */
static int lsm303dlhc_mag_event(I2CSlave *i2c, enum i2c_event event)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    switch (event) {
    case I2C_START_SEND:
        break;
    case I2C_START_RECV:
        lsm303dlhc_mag_read(s);
        break;
    case I2C_FINISH:
        break;
    case I2C_NACK:
        break;
    }

    s->len = 0;
    return 0;
}

/*
 * Device data description using VMSTATE macros.
 */
static const VMStateDescription vmstate_lsm303dlhc_mag = {
    .name = "LSM303DLHC_MAG",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {

        VMSTATE_I2C_SLAVE(parent_obj, LSM303DLHCMagState),
        VMSTATE_UINT8(len, LSM303DLHCMagState),
        VMSTATE_UINT8_ARRAY(buf, LSM303DLHCMagState, 6),
        VMSTATE_UINT8(pointer, LSM303DLHCMagState),
        VMSTATE_UINT8(cra, LSM303DLHCMagState),
        VMSTATE_UINT8(crb, LSM303DLHCMagState),
        VMSTATE_UINT8(mr, LSM303DLHCMagState),
        VMSTATE_INT16(x, LSM303DLHCMagState),
        VMSTATE_INT16(z, LSM303DLHCMagState),
        VMSTATE_INT16(y, LSM303DLHCMagState),
        VMSTATE_UINT8(sr, LSM303DLHCMagState),
        VMSTATE_UINT8(ira, LSM303DLHCMagState),
        VMSTATE_UINT8(irb, LSM303DLHCMagState),
        VMSTATE_UINT8(irc, LSM303DLHCMagState),
        VMSTATE_INT16(temperature, LSM303DLHCMagState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Put the device into post-reset default state.
 */
static void lsm303dlhc_mag_default_cfg(LSM303DLHCMagState *s)
{
    /* Set the device into is default reset state. */
    s->len = 0;
    s->pointer = 0;         /* Current register. */
    memset(s->buf, 0, sizeof(s->buf));
    s->cra = 0x10;          /* Temp Enabled = 0, Data Rate = 15.0 Hz. */
    s->crb = 0x20;          /* Gain = +/- 1.3 Gauss. */
    s->mr = 0x3;            /* Operating Mode = Sleep. */
    s->x = 0;
    s->z = 0;
    s->y = 0;
    s->sr = 0x1;            /* DRDY = 1. */
    s->ira = 0x48;
    s->irb = 0x34;
    s->irc = 0x33;
    s->temperature = 0;     /* Default to 0 degrees C (0/8 lsb = 0 C). */
}

/*
 * Callback handler when DeviceState 'reset' is set to true.
 */
static void lsm303dlhc_mag_reset(DeviceState *dev)
{
    I2CSlave *i2c = I2C_SLAVE(dev);
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    /* Set the device into its default reset state. */
    lsm303dlhc_mag_default_cfg(s);
}

/*
 * Initialisation of any public properties.
 */
static void lsm303dlhc_mag_initfn(Object *obj)
{
    object_property_add(obj, "mag-x", "int",
                lsm303dlhc_mag_get_x,
                lsm303dlhc_mag_set_x, NULL, NULL);

    object_property_add(obj, "mag-y", "int",
                lsm303dlhc_mag_get_y,
                lsm303dlhc_mag_set_y, NULL, NULL);

    object_property_add(obj, "mag-z", "int",
                lsm303dlhc_mag_get_z,
                lsm303dlhc_mag_set_z, NULL, NULL);

    object_property_add(obj, "temperature", "int",
                lsm303dlhc_mag_get_temperature,
                lsm303dlhc_mag_set_temperature, NULL, NULL);
}

/*
 * Set the virtual method pointers (bus state change, tx/rx, etc.).
 */
static void lsm303dlhc_mag_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = lsm303dlhc_mag_reset;
    dc->vmsd = &vmstate_lsm303dlhc_mag;
    k->event = lsm303dlhc_mag_event;
    k->recv = lsm303dlhc_mag_recv;
    k->send = lsm303dlhc_mag_send;
}

static const TypeInfo lsm303dlhc_mag_info = {
    .name = TYPE_LSM303DLHC_MAG,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(LSM303DLHCMagState),
    .instance_init = lsm303dlhc_mag_initfn,
    .class_init = lsm303dlhc_mag_class_init,
};

static void lsm303dlhc_mag_register_types(void)
{
    type_register_static(&lsm303dlhc_mag_info);
}

type_init(lsm303dlhc_mag_register_types)
