/*
 * Raspberry Pi emulation (c) 2017 Marcin Chojnacki
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/bcm2835_rng.h"

static uint64_t bcm2835_rng_read(void *opaque, hwaddr offset,
    unsigned size)
{
    BCM2835RngState *s = (BCM2835RngState *)opaque;
    uint32_t res = 0;

    assert(size == 4);

    switch (offset) {
    case 0x0:    /* rng_ctrl */
        res = s->rng_ctrl;
        break;
    case 0x4:    /* rng_status */
        res = s->rng_status | (1 << 24);
        break;
    case 0x8:    /* rng_data */
        res = rand();
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_rng_read: Bad offset %x\n", (int)offset);
        res = 0;
        break;
    }

    return res;
}

static void bcm2835_rng_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size)
{
    BCM2835RngState *s = (BCM2835RngState *)opaque;

    assert(size == 4);

    switch (offset) {
    case 0x0:    /* rng_ctrl */
        s->rng_ctrl = value;
        break;
    case 0x4:    /* rng_status */
        s->rng_status = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_rng_write: Bad offset %x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps bcm2835_rng_ops = {
    .read = bcm2835_rng_read,
    .write = bcm2835_rng_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_rng = {
    .name = TYPE_BCM2835_RNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_rng_init(Object *obj)
{
    BCM2835RngState *s = BCM2835_RNG(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_rng_ops, s,
                          TYPE_BCM2835_RNG, 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void bcm2835_rng_realize(DeviceState *dev, Error **errp)
{
    BCM2835RngState *s = BCM2835_RNG(dev);

    s->rng_ctrl = 0;
    s->rng_status = 0;
}

static void bcm2835_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_rng_realize;
    dc->vmsd = &vmstate_bcm2835_rng;
}

static TypeInfo bcm2835_rng_info = {
    .name          = TYPE_BCM2835_RNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835RngState),
    .class_init    = bcm2835_rng_class_init,
    .instance_init = bcm2835_rng_init,
};

static void bcm2835_rng_register_types(void)
{
    type_register_static(&bcm2835_rng_info);
}

type_init(bcm2835_rng_register_types)
