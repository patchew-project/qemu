/*
 * BCM2835 Clock subsystem (poor man's version)
 *
 * Copyright (C) 2018 Guenter Roeck <linux@roeck-us.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "crypto/random.h"
#include "hw/misc/bcm2835_cprman.h"

static uint64_t bcm2835_cprman_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    BCM2835CprmanState *s = (BCM2835CprmanState *)opaque;
    uint32_t res = 0;

    assert(size == 4);

    if (offset / 4 < CPRMAN_NUM_REGS) {
        res = s->regs[offset / 4];
    }

    return res;
}

#define CM_PASSWORD             0x5a000000
#define CM_PASSWORD_MASK        0xff000000

static void bcm2835_cprman_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    BCM2835CprmanState *s = (BCM2835CprmanState *)opaque;

    assert(size == 4);

    if ((value & 0xff000000) == CM_PASSWORD &&
        offset / 4 < CPRMAN_NUM_REGS)
            s->regs[offset / 4] = value & ~CM_PASSWORD_MASK;
}

static const MemoryRegionOps bcm2835_cprman_ops = {
    .read = bcm2835_cprman_read,
    .write = bcm2835_cprman_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_cprman = {
    .name = TYPE_BCM2835_CPRMAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BCM2835CprmanState, CPRMAN_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_cprman_init(Object *obj)
{
    BCM2835CprmanState *s = BCM2835_CPRMAN(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_cprman_ops, s,
                          TYPE_BCM2835_CPRMAN, 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

#define CM_GNRICCTL        (0x000 / 4)
#define CM_VECCTL        (0x0f8 / 4)
#define CM_DFTCTL        (0x168 / 4)
#define CM_EMMCCTL        (0x1c0 / 4)
#define A2W_PLLA_CTRL        (0x1100 / 4)
#define A2W_PLLB_CTRL        (0x11e0 / 4)

static void bcm2835_cprman_reset(DeviceState *dev)
{
    BCM2835CprmanState *s = BCM2835_CPRMAN(dev);
    int i;

    /*
     * Available information suggests that CPRMAN registers have default
     * values which are not overwritten by ROMMON (u-boot). The hardware
     * default values are unknown at this time.
     * The default values selected here are necessary and sufficient
     * to boot Linux directly (on raspi2 and raspi3). The selected
     * values enable all clocks and set clock rates to match their
     * parent rates.
     */
    for (i = CM_GNRICCTL; i <= CM_VECCTL; i += 2) {
        s->regs[i] = 0x11;
        s->regs[i + 1] = 0x1000;
    }
    for (i = CM_DFTCTL; i <= CM_EMMCCTL; i += 2) {
        s->regs[i] = 0x11;
        s->regs[i + 1] = 0x1000;
    }
    for (i = A2W_PLLA_CTRL; i <= A2W_PLLB_CTRL; i += 8) {
        s->regs[i] = 0x10001;
    }
}

static void bcm2835_cprman_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2835_cprman_reset;
    dc->vmsd = &vmstate_bcm2835_cprman;
}

static TypeInfo bcm2835_cprman_info = {
    .name          = TYPE_BCM2835_CPRMAN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835CprmanState),
    .class_init    = bcm2835_cprman_class_init,
    .instance_init = bcm2835_cprman_init,
};

static void bcm2835_cprman_register_types(void)
{
    type_register_static(&bcm2835_cprman_info);
}

type_init(bcm2835_cprman_register_types)
