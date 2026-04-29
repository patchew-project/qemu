/*
 * PIC32MK I2C × 4 (I2C1–I2C4)
 * Datasheet: DS60001519E, §24
 *
 * Each I2C instance is a SysBusDevice that stubs the register file and
 * attaches a QEMU i2c_bus for downstream device models.
 * Master-mode state machine is Phase 2B; firmware init (I2CxCON writes)
 * succeeds silently.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/i2c/i2c.h"
#include "hw/mips/pic32mk.h"

#define TYPE_PIC32MK_I2C    "pic32mk-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKI2CState, PIC32MK_I2C)

struct PIC32MKI2CState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t con;   /* I2CxCON */
    uint32_t stat;  /* I2CxSTAT */
    uint32_t add;   /* I2CxADD */
    uint32_t msk;   /* I2CxMSK */
    uint32_t trn;   /* I2CxTRN — TX data */
    uint32_t rcv;   /* I2CxRCV — RX data (read-only) */

    I2CBus  *bus;
    qemu_irq irq_master;   /* master interrupt */
    qemu_irq irq_slave;    /* slave interrupt */
    qemu_irq irq_bus;      /* bus-collision interrupt */
};

/* I2CxSTAT bits */
#define I2C_STAT_TRSTAT     (1u << 14)  /* transmit status */
#define I2C_STAT_ACKSTAT    (1u << 15)  /* ACK status */

/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

static uint32_t *i2c_find_reg(PIC32MKI2CState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_I2CxCON:
        return &s->con;
    case PIC32MK_I2CxSTAT:
        return &s->stat;
    case PIC32MK_I2CxADD:
        return &s->add;
    case PIC32MK_I2CxMSK:
        return &s->msk;
    case PIC32MK_I2CxTRN:
        return &s->trn;
    case PIC32MK_I2CxRCV:
        return &s->rcv;
    default:
        return NULL;
    }
}

static uint64_t i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKI2CState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;
    uint32_t *reg = i2c_find_reg(s, base);

    if (reg) {
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_i2c: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void i2c_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKI2CState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;
    uint32_t *reg = i2c_find_reg(s, base);

    if (!reg) {
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_i2c: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        return;
    }

    /* TRN write: transmit byte — stub logs but does not transfer */
    if (base == PIC32MK_I2CxTRN) {
        s->trn = (uint32_t)val;
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_i2c: TX write 0x%02x (I2C master not implemented)\n",
                      (uint32_t)val & 0xFF);
        return;
    }

    /* STAT is partially read-only; accept writes for CLR operations */
    apply_sci(reg, (uint32_t)val, sub);
}

static const MemoryRegionOps i2c_ops = {
    .read       = i2c_read,
    .write      = i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void pic32mk_i2c_reset(DeviceState *dev)
{
    PIC32MKI2CState *s = PIC32MK_I2C(dev);
    s->con  = 0;
    s->stat = 0;
    s->add  = 0;
    s->msk  = 0;
    s->trn  = 0xFF;
    s->rcv  = 0;
    qemu_irq_lower(s->irq_master);
    qemu_irq_lower(s->irq_slave);
    qemu_irq_lower(s->irq_bus);
}

static void pic32mk_i2c_init(Object *obj)
{
    PIC32MKI2CState *s = PIC32MK_I2C(obj);

    memory_region_init_io(&s->mr, obj, &i2c_ops, s,
                          TYPE_PIC32MK_I2C, PIC32MK_I2C_BLOCK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_master);  /* index 0 */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_slave);   /* index 1 */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_bus);     /* index 2 */

    s->bus = i2c_init_bus(DEVICE(obj), "i2c");
}

static void pic32mk_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_i2c_reset);
}

static const TypeInfo pic32mk_i2c_info = {
    .name          = TYPE_PIC32MK_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKI2CState),
    .instance_init = pic32mk_i2c_init,
    .class_init    = pic32mk_i2c_class_init,
};

static void pic32mk_i2c_register_types(void)
{
    type_register_static(&pic32mk_i2c_info);
}

type_init(pic32mk_i2c_register_types)
