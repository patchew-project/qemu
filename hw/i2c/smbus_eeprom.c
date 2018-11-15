/*
 * QEMU SMBus EEPROM device
 *
 * Copyright (c) 2007 Arastra, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"
#include "hw/i2c/smbus_eeprom.h"

//#define DEBUG

typedef struct SMBusEEPROMDevice {
    SMBusDevice smbusdev;
    void *data;
    uint8_t offset;
} SMBusEEPROMDevice;

static uint8_t eeprom_receive_byte(SMBusDevice *dev)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    uint8_t *data = eeprom->data;
    uint8_t val = data[eeprom->offset++];

#ifdef DEBUG
    printf("eeprom_receive_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
    return val;
}

static int eeprom_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    uint8_t *data = eeprom->data;

#ifdef DEBUG
    printf("eeprom_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);
#endif
    /* len is guaranteed to be > 0 */
    eeprom->offset = buf[0];
    buf++;
    len--;

    for (; len > 0; len--) {
        data[eeprom->offset] = *buf++;
        eeprom->offset = (eeprom->offset + 1) % 256;
    }

    return 0;
}

static void smbus_eeprom_realize(DeviceState *dev, Error **errp)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *)dev;

    eeprom->offset = 0;
}

static Property smbus_eeprom_properties[] = {
    DEFINE_PROP_PTR("data", SMBusEEPROMDevice, data),
    DEFINE_PROP_END_OF_LIST(),
};

static void smbus_eeprom_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    dc->realize = smbus_eeprom_realize;
    sc->receive_byte = eeprom_receive_byte;
    sc->write_data = eeprom_write_data;
    dc->props = smbus_eeprom_properties;
    /* Reason: pointer property "data" */
    dc->user_creatable = false;
}

static const TypeInfo smbus_eeprom_info = {
    .name          = "smbus-eeprom",
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusEEPROMDevice),
    .class_init    = smbus_eeprom_class_initfn,
};

static void smbus_eeprom_register_types(void)
{
    type_register_static(&smbus_eeprom_info);
}

type_init(smbus_eeprom_register_types)

void smbus_eeprom_init_one(I2CBus *smbus, uint8_t address, uint8_t *eeprom_buf)
{
    DeviceState *dev;

    dev = qdev_create((BusState *) smbus, "smbus-eeprom");
    qdev_prop_set_uint8(dev, "address", address);
    qdev_prop_set_ptr(dev, "data", eeprom_buf);
    qdev_init_nofail(dev);
}

void smbus_eeprom_init(I2CBus *smbus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int eeprom_spd_size)
{
    int i;
    uint8_t *eeprom_buf = g_malloc0(8 * 256); /* XXX: make this persistent */
    if (eeprom_spd_size > 0) {
        memcpy(eeprom_buf, eeprom_spd, eeprom_spd_size);
    }

    for (i = 0; i < nb_eeprom; i++) {
        smbus_eeprom_init_one(smbus, 0x50 + i, eeprom_buf + (i * 256));
    }
}
