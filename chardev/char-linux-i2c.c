/*
 * QEMU System Emulator
 * Linux I2C device support as a character device.
 *
 * Copyright (c) 2019 Ernest Esene <eroken1@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "qemu-common.h"
#include "io/channel-file.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"

#include "chardev/char-fd.h"
#include "chardev/char.h"

#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define CHR_IOCTL_I2C_SET_ADDR 1

#define CHR_I2C_ADDR_10BIT_MAX 1023
#define CHR_I2C_ADDR_7BIT_MAX 127

static int i2c_ioctl(Chardev *chr, int cmd, void *arg)
{
    FDChardev *fd_chr = FD_CHARDEV(chr);
    QIOChannelFile *floc = QIO_CHANNEL_FILE(fd_chr->ioc_in);
    int fd = floc->fd;
    int addr;
    unsigned long funcs;

    switch (cmd) {
    case CHR_IOCTL_I2C_SET_ADDR:
        addr = (intptr_t)arg;

        if (addr > CHR_I2C_ADDR_7BIT_MAX) {
            if (ioctl(fd, I2C_FUNCS, &funcs) < 0) {
                goto err;
            }
            if (!(funcs & I2C_FUNC_10BIT_ADDR)) {
                goto err;
            }
            if (ioctl(fd, I2C_TENBIT, addr) < 0) {
                goto err;
            }
        } else {
            if (ioctl(fd, I2C_SLAVE, addr) < 0) {
                goto err;
            }
        }
        break;

    default:
        return -ENOTSUP;
    }
    return 0;
err:
    return -ENOTSUP;
}

static void qmp_chardev_open_i2c(Chardev *chr, ChardevBackend *backend,
                                 bool *be_opened, Error **errp)
{
    ChardevI2c *i2c = backend->u.i2c.data;
    void *addr;
    int fd;

    fd = qmp_chardev_open_file_source(i2c->device, O_RDWR | O_NONBLOCK, errp);
    if (fd < 0) {
        return;
    }
    qemu_set_nonblock(fd);
    qemu_chr_open_fd(chr, fd, fd);
    addr = (void *)(intptr_t)i2c->address;
    i2c_ioctl(chr, CHR_IOCTL_I2C_SET_ADDR, addr);
}

static void qemu_chr_parse_i2c(QemuOpts *opts, ChardevBackend *backend,
                               Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    long address = qemu_opt_get_number(opts, "address", LONG_MAX);
    ChardevI2c *i2c;

    if (device == NULL) {
        error_setg(errp, "chardev: i2c: no device path given");
        return;
    }
    if (address < 0 || address > CHR_I2C_ADDR_10BIT_MAX) {
        error_setg(errp, "chardev: i2c: device address out of range");
        return;
    }
    backend->type = CHARDEV_BACKEND_KIND_I2C;
    i2c = backend->u.i2c.data = g_new0(ChardevI2c, 1);
    qemu_chr_parse_common(opts, qapi_ChardevI2c_base(i2c));
    i2c->device = g_strdup(device);
    i2c->address = (int16_t)address;
}

static void char_i2c_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_i2c;
    cc->open = qmp_chardev_open_i2c;
    cc->chr_ioctl = i2c_ioctl;
}

static const TypeInfo char_i2c_type_info = {
    .name = TYPE_CHARDEV_I2C,
    .parent = TYPE_CHARDEV_FD,
    .class_init = char_i2c_class_init,
};

static void register_types(void)
{
    type_register_static(&char_i2c_type_info);
}

type_init(register_types);
