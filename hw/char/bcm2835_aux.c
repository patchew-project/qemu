/*
 * BCM2835 (Raspberry Pi / Pi 2) Aux block (mini UART and SPI).
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GPL.
 *
 * At present only the core UART functions (data path for tx/rx) are
 * implemented. The following features/registers are unimplemented:
 *  - Extra control
 *  - SPI interfaces
 */

#include "qemu/osdep.h"
#include "hw/char/bcm2835_aux.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"

#define AUX_IRQ         0x0
#define AUX_ENABLES     0x4
#define AUX_MU_REGS     0x40
#define AUX_MU_CNTL_REG 0x60
#define AUX_MU_STAT_REG 0x64
#define AUX_MU_BAUD_REG 0x68

static uint64_t bcm2835_aux_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835AuxState *s = opaque;
    uint32_t res;

    switch (offset) {
    case AUX_IRQ:
        return s->serial->iir != 0;

    case AUX_ENABLES:
        return 1; /* mini UART permanently enabled */

    case AUX_MU_CNTL_REG:
        return 0x3; /* tx, rx enabled */

    case AUX_MU_STAT_REG:
        res = 0x30e; /* space in the output buffer, empty tx fifo, idle tx/rx */
        res |= fifo8_num_used(&s->serial->xmit_fifo) << 24;
        res |= fifo8_num_used(&s->serial->recv_fifo) << 16;
        res |= fifo8_is_empty(&s->serial->xmit_fifo) << 8;
        res |= fifo8_is_full(&s->serial->xmit_fifo) << 5;
        res |= fifo8_is_full(&s->serial->recv_fifo) << 4;
        res |= !fifo8_is_full(&s->serial->xmit_fifo) << 1;
        res |= !fifo8_is_empty(&s->serial->recv_fifo) << 0;
        return res;

    case AUX_MU_BAUD_REG:
        return s->serial->divider;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }
}

static void bcm2835_aux_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    BCM2835AuxState *s = opaque;

    switch (offset) {
    case AUX_ENABLES:
        if (value != 1) {
            qemu_log_mask(LOG_UNIMP, "%s: unsupported attempt to enable SPI "
                          "or disable UART\n", __func__);
        }
        break;

    case AUX_MU_CNTL_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_CNTL_REG unsupported\n", __func__);
        break;

    case AUX_MU_BAUD_REG:
        serial_set_divider(s->serial, value);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps bcm2835_aux_ops = {
    .read = bcm2835_aux_read,
    .write = bcm2835_aux_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_aux = {
    .name = TYPE_BCM2835_AUX,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_aux_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835AuxState *s = BCM2835_AUX(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_aux_ops, s,
                          TYPE_BCM2835_AUX, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->serial = serial_mm_init(&s->iomem, AUX_MU_REGS, 2, s->irq, 2419200,
                               serial_hd(1), DEVICE_LITTLE_ENDIAN);
}

static void bcm2835_aux_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_bcm2835_aux;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo bcm2835_aux_info = {
    .name          = TYPE_BCM2835_AUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835AuxState),
    .instance_init = bcm2835_aux_init,
    .class_init    = bcm2835_aux_class_init,
};

static void bcm2835_aux_register_types(void)
{
    type_register_static(&bcm2835_aux_info);
}

type_init(bcm2835_aux_register_types)
