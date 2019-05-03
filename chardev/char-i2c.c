/*
 * QEMU System Emulator
 *
 * Copyright (c) 2019 Ernest Esene <eroken1@gmail.com>
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
#include "qapi/error.h"
#include "qemu/option.h"
#include "qemu-common.h"
#include "io/channel-file.h"

#include "chardev/char-fd.h"
#include "chardev/char.h"

#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define CHR_IOCTL_I2C_SET_ADDR 1

#define CHR_I2C_ADDR_10BIT_MAX 1023
#define CHR_I2C_ADDR_7BIT_MAX 127

void qemu_set_block(int fd);

static int i2c_ioctl(Chardev *chr, int cmd, void *arg)
{
    FDChardev *fd_chr = FD_CHARDEV(chr);
    QIOChannelFile *floc = QIO_CHANNEL_FILE(fd_chr->ioc_in);
    int fd = floc->fd;
    int addr;

    switch (cmd) {
        case CHR_IOCTL_I2C_SET_ADDR:
            addr = (int) (long) arg;

            if (addr > CHR_I2C_ADDR_7BIT_MAX) {
                /*TODO: check if adapter support 10-bit addr
                I2C_FUNC_10BIT_ADDR */
                if (ioctl(fd, I2C_TENBIT, addr) < 0) {
                    goto err;
                }
            }
            else {
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

    fd = qmp_chardev_open_file_source(i2c->device, O_RDWR | O_NONBLOCK,
                                      errp);
    if (fd < 0) {
       return;
    }
    qemu_set_block(fd);
    qemu_chr_open_fd(chr, fd, fd);
    addr = (void *) (long) i2c->address;
    i2c_ioctl(chr, CHR_IOCTL_I2C_SET_ADDR, addr);
}

static void qemu_chr_parse_i2c(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    const char *addr = qemu_opt_get(opts, "address");
    long address;
    ChardevI2c *i2c;
    
    if (device == NULL) {
        error_setg(errp, "chardev: linux-i2c: no device path given");
        return;
    }
    if (addr == NULL) {
        error_setg(errp, "chardev: linux-i2c: no device address given");
        return;
    }
    address = strtol(addr, NULL, 0);
    if (address < 0 || address > CHR_I2C_ADDR_10BIT_MAX) {
        error_setg(errp, "chardev: linux-i2c: invalid device address given");
        return;
    }
    backend->type = CHARDEV_BACKEND_KIND_I2C;
    i2c = backend->u.i2c.data = g_new0(ChardevI2c, 1);
    qemu_chr_parse_common(opts, qapi_ChardevI2c_base(i2c));
    i2c->device = g_strdup(device);
    i2c->address = (int16_t) address;
}

static void char_i2c_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_i2c;
    cc->open =  qmp_chardev_open_i2c;
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
