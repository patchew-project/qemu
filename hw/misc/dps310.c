// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2017 Joel Stanley <joel@jms.id.au>, IBM Corporation
 *
 * Infineon DPS310 temperature and himidity sensor
 *
 * https://www.infineon.com/cms/en/product/sensor/pressure-sensors/pressure-sensors-for-iot/dps310/
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "migration/vmstate.h"

#define NUM_REGISTERS   0x32

typedef struct DPS310State {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/
    uint8_t regs[NUM_REGISTERS];

    int16_t pressure, temperature;

    uint8_t len;
    uint8_t buf[2];
    uint8_t pointer;

} DPS310State;

typedef struct DPS310Class {
    I2CSlaveClass parent_class;
} DPS310Class;

#define TYPE_DPS310 "dps310"
#define DPS310(obj) OBJECT_CHECK(DPS310State, (obj), TYPE_DPS310)

#define DPS310_CLASS(klass) \
     OBJECT_CLASS_CHECK(DPS310Class, (klass), TYPE_DPS310)
#define DPS310_GET_CLASS(obj) \
     OBJECT_GET_CLASS(DPS310Class, (obj), TYPE_DPS310)

#define DPS310_PRS_B2           0x00
#define DPS310_PRS_B1           0x01
#define DPS310_PRS_B0           0x02
#define DPS310_TMP_B2           0x03
#define DPS310_TMP_B1           0x04
#define DPS310_TMP_B0           0x05
#define DPS310_PRS_CFG          0x06
#define DPS310_TMP_CFG          0x07
#define  DPS310_TMP_RATE_BITS   GENMASK(6, 4)
#define DPS310_MEAS_CFG         0x08
#define  DPS310_MEAS_CTRL_BITS  GENMASK(2, 0)
#define   DPS310_PRESSURE_EN    BIT(0)
#define   DPS310_TEMP_EN        BIT(1)
#define   DPS310_BACKGROUND     BIT(2)
#define  DPS310_PRS_RDY         BIT(4)
#define  DPS310_TMP_RDY         BIT(5)
#define  DPS310_SENSOR_RDY      BIT(6)
#define  DPS310_COEF_RDY        BIT(7)
#define DPS310_CFG_REG          0x09
#define DPS310_RESET            0x0c
#define  DPS310_RESET_MAGIC     (BIT(0) | BIT(3))
#define DPS310_COEF_BASE        0x10
#define DPS310_COEF_LAST        0x21
#define DPS310_COEF_SRC         0x28

static void dps310_reset(DeviceState *dev)
{
    DPS310State *s = DPS310(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->pointer = 0;

    s->regs[0x00] = 0xfe;
    s->regs[0x01] = 0x2f;
    s->regs[0x02] = 0xee;
    s->regs[0x03] = 0x02;
    s->regs[0x04] = 0x69;
    s->regs[0x05] = 0xa6;
    s->regs[0x06] = 0x00;
    s->regs[0x07] = 0x80;
    s->regs[0x08] = 0xc7;

    s->regs[0x0d] = 0x10;
    s->regs[0x0e] = 0x00;
    s->regs[0x0f] = 0x00;
    s->regs[0x10] = 0x0e;
    s->regs[0x11] = 0x1e;
    s->regs[0x12] = 0xdd;
    s->regs[0x13] = 0x13;
    s->regs[0x14] = 0xca;
    s->regs[0x15] = 0x5f;
    s->regs[0x16] = 0x21;
    s->regs[0x17] = 0x52;
    s->regs[0x18] = 0xf9;
    s->regs[0x19] = 0xc6;
    s->regs[0x1a] = 0x04;
    s->regs[0x1b] = 0xd1;
    s->regs[0x1c] = 0xdb;
    s->regs[0x1d] = 0x47;
    s->regs[0x1e] = 0x00;
    s->regs[0x1f] = 0x5b;
    s->regs[0x20] = 0xfb;
    s->regs[0x21] = 0x3a;
    s->regs[0x22] = 0x00;
    s->regs[0x23] = 0x00;
    s->regs[0x24] = 0x20;
    s->regs[0x25] = 0x49;
    s->regs[0x26] = 0x4e;
    s->regs[0x27] = 0xa5;
    s->regs[0x28] = 0x90;

    s->regs[0x30] = 0x60;
    s->regs[0x31] = 0x15;

    /* TODO: assert these after some timeout ? */
    s->regs[DPS310_MEAS_CFG] = DPS310_COEF_RDY | DPS310_SENSOR_RDY
        | DPS310_TMP_RDY | DPS310_PRS_RDY;
}

static void dps310_get_pressure(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    DPS310State *s = DPS310(obj);
    int64_t value;

    /* TODO */
    value = s->pressure;

    visit_type_int(v, name, &value, errp);
}

static void dps310_get_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    DPS310State *s = DPS310(obj);
    int64_t value;

    /* TODO */
    value = s->temperature;

    visit_type_int(v, name, &value, errp);
}

static void dps310_set_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    DPS310State *s = DPS310(obj);
    Error *local_err = NULL;
    int64_t temp;

    visit_type_int(v, name, &temp, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* TODO */
    if (temp >= 200 || temp < -100) {
        error_setg(errp, "value %" PRId64 ".%03" PRIu64 " °C is out of range",
                   temp / 1000, temp % 1000);
        return;
    }

    s->temperature = temp;
}

static void dps310_set_pressure(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    DPS310State *s = DPS310(obj);
    Error *local_err = NULL;
    int64_t pres;

    visit_type_int(v, name, &pres, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* TODO */
    if (pres >= 200 || pres < -100) {
        error_setg(errp, "value %" PRId64 ".%03" PRIu64 " is out of range",
                   pres / 1000, pres % 1000);
        return;
    }

    s->pressure = pres;
}

static void dps310_read(DPS310State *s)
{
    /* Test should be >=, but we want to include register 0x32 */
    if (s->pointer > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: register 0x%02x out of bounds\n",
                      __func__, s->pointer);
        return;
    }

    s->len = 0;

    switch (s->pointer) {
    case DPS310_PRS_B2:
    case DPS310_PRS_B1:
    case DPS310_PRS_B0:
    case DPS310_TMP_B2:
    case DPS310_TMP_B1:
    case DPS310_TMP_B0:
    case DPS310_PRS_CFG:
    case DPS310_TMP_CFG:
    case DPS310_MEAS_CFG:
    case DPS310_CFG_REG:
    case DPS310_COEF_BASE...DPS310_COEF_LAST:
    case DPS310_COEF_SRC:
        s->buf[s->len++] = s->regs[s->pointer];
        break;
    case 0x32:
        /* Undocumented register to indicate workaround not required */
        s->buf[s->len++] = 0x2;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: register 0x%02x unimplemented\n",
                      __func__, s->pointer);
        return;
    }
}

static void dps310_write(DPS310State *s)
{
    if (s->pointer >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: register %d out of bounds\n",
                      __func__, s->pointer);
        return;
    }

    switch (s->pointer) {
    case DPS310_RESET:
        if (s->buf[0] == DPS310_RESET_MAGIC) {
            dps310_reset(DEVICE(s));
        }
        break;
    case DPS310_PRS_CFG:
    case DPS310_TMP_CFG:
    case DPS310_MEAS_CFG:
    case DPS310_CFG_REG:
        s->regs[s->pointer] = s->buf[0];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: register 0x%02x unimplemented\n",
                      __func__, s->pointer);
        return;
    }
}

static uint8_t dps310_rx(I2CSlave *i2c)
{
    DPS310State *s = DPS310(i2c);

    if (s->len < 2) {
        return s->buf[s->len++];
    } else {
        return 0xff;
    }
}

static int dps310_tx(I2CSlave *i2c, uint8_t data)
{
    DPS310State *s = DPS310(i2c);

    if (s->len == 0) {
        /*
         * first byte is the register pointer for a read or write
         * operation
         */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        /*
         * second byte is the data to write. The device only supports
         * one byte writes
         */
        s->buf[0] = data;
        dps310_write(s);
    }

    return 0;
}

static int dps310_event(I2CSlave *i2c, enum i2c_event event)
{
    DPS310State *s = DPS310(i2c);

    if (event == I2C_START_RECV) {
        dps310_read(s);
    }

    s->len = 0;
    return 0;
}

static const VMStateDescription vmstate_dps310 = {
    .name = "DPS310",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, DPS310State),
        VMSTATE_UINT8_ARRAY(buf, DPS310State, 2),
        VMSTATE_UINT8_ARRAY(regs, DPS310State, NUM_REGISTERS),
        VMSTATE_UINT8(pointer, DPS310State),
        VMSTATE_INT16(temperature, DPS310State),
        VMSTATE_INT16(pressure, DPS310State),
        VMSTATE_I2C_SLAVE(i2c, DPS310State),
        VMSTATE_END_OF_LIST()
    }
};

static void dps310_initfn(Object *obj)
{
    object_property_add(obj, "temperature", "int",
                        dps310_get_temperature,
                        dps310_set_temperature, NULL, NULL);
    object_property_add(obj, "pressure", "int",
                        dps310_get_pressure,
                        dps310_set_pressure, NULL, NULL);
}

static void dps310_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = dps310_event;
    k->recv = dps310_rx;
    k->send = dps310_tx;
    dc->reset = dps310_reset;
    dc->vmsd = &vmstate_dps310;
}

static const TypeInfo dps310_info = {
    .name          = TYPE_DPS310,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DPS310State),
    .instance_init = dps310_initfn,
    .class_init    = dps310_class_init,
};

static void dps310_register_types(void)
{
    type_register_static(&dps310_info);
}

type_init(dps310_register_types)
