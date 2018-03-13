/*
 * I2C device passthrough HACK
 *
 * Copyright (c) 2018 Wolfram Sang, Sang Engineering, Renesas Electronics
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 *
 * Example to use:
 *     -device host-i2cdev,address=0x64,file=/dev/i2c-0,hostaddr=0x50
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"

/* Ugh, problem! QEMU defines I2C_SLAVE, too */
#undef I2C_SLAVE
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define ERR(FMT, ...) fprintf(stderr, TYPE_HOST_I2CDEV " : " FMT, \
                            ## __VA_ARGS__)

#define TYPE_HOST_I2CDEV "host-i2cdev"
#define HOST_I2CDEV(obj) OBJECT_CHECK(HostI2CDevState, (obj), TYPE_HOST_I2CDEV)

typedef struct HostI2CDevState {
    I2CSlave parent_obj;
    char *file;
    int fd;
    uint32_t hostaddr;
} HostI2CDevState;

static int host_i2cdev_recv(I2CSlave *s)
{
    HostI2CDevState *i2cdev = HOST_I2CDEV(s);
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data args = {
        .read_write = I2C_SMBUS_READ,
        .size = I2C_SMBUS_BYTE,
        .data = &data,
    };
    int err;

    err = ioctl(i2cdev->fd, I2C_SMBUS, &args);

    return err == 0 ? data.byte : 0;
}

static int host_i2cdev_send(I2CSlave *s, uint8_t data)
{
    /* We don't support writes */
    return -1;
}

static int host_i2cdev_init(I2CSlave *i2c)
{
    HostI2CDevState *i2cdev = HOST_I2CDEV(i2c);

    if (!i2cdev->file) {
        ERR("file is required!\n");
        exit(1);
    }

    i2cdev->fd = qemu_open(i2cdev->file, O_RDWR);

    if (i2cdev->fd < 0) {
        ERR("file can't be opened!\n");
        exit(1);
    }

    return ioctl(i2cdev->fd, I2C_SLAVE, i2cdev->hostaddr ?: i2c->address);
}

static Property host_i2cdev_props[] = {
    DEFINE_PROP_STRING("file", HostI2CDevState, file),
    DEFINE_PROP_UINT32("hostaddr", HostI2CDevState, hostaddr, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void host_i2cdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init = &host_i2cdev_init;
    k->recv = &host_i2cdev_recv;
    k->send = &host_i2cdev_send;

    dc->props = host_i2cdev_props;
}

static
const TypeInfo host_i2cdev_type = {
    .name = TYPE_HOST_I2CDEV,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(HostI2CDevState),
    .class_size = sizeof(I2CSlaveClass),
    .class_init = host_i2cdev_class_init,
};

static void host_i2cdev_register(void)
{
    type_register_static(&host_i2cdev_type);
}

type_init(host_i2cdev_register)
