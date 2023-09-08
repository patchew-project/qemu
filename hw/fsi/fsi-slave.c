/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface slave
 */

#include "qemu/osdep.h"

#include "qemu/bitops.h"
#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/fsi/fsi-slave.h"

#define TO_REG(x)                               ((x) >> 2)

#define FSI_SMODE               TO_REG(0x00)
#define   FSI_SMODE_WSTART      BE_BIT(0)
#define   FSI_SMODE_AUX_EN      BE_BIT(1)
#define   FSI_SMODE_SLAVE_ID    BE_GENMASK(6, 7)
#define   FSI_SMODE_ECHO_DELAY  BE_GENMASK(8, 11)
#define   FSI_SMODE_SEND_DELAY  BE_GENMASK(12, 15)
#define   FSI_SMODE_LBUS_DIV    BE_GENMASK(20, 23)
#define   FSI_SMODE_BRIEF_LEFT  BE_GENMASK(24, 27)
#define   FSI_SMODE_BRIEF_RIGHT BE_GENMASK(28, 31)

#define FSI_SDMA                TO_REG(0x04)
#define FSI_SISC                TO_REG(0x08)
#define FSI_SCISC               TO_REG(0x08)
#define FSI_SISM                TO_REG(0x0c)
#define FSI_SISS                TO_REG(0x10)
#define FSI_SSISM               TO_REG(0x10)
#define FSI_SCISM               TO_REG(0x14)

static uint64_t fsi_slave_read(void *opaque, hwaddr addr, unsigned size)
{
    FSISlaveState *s = FSI_SLAVE(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: read @0x%" HWADDR_PRIx " size=%d\n",
                  __func__, addr, size);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->regs[TO_REG(addr)];
}

static void fsi_slave_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    FSISlaveState *s = FSI_SLAVE(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: write @0x%" HWADDR_PRIx " size=%d "
                  "value=%"PRIx64"\n", __func__, addr, size, data);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    s->regs[TO_REG(addr)] = data;
}

static const struct MemoryRegionOps fsi_slave_ops = {
    .read = fsi_slave_read,
    .write = fsi_slave_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void fsi_slave_init(Object *o)
{
    FSISlaveState *s = FSI_SLAVE(o);

    memory_region_init_io(&s->iomem, OBJECT(s), &fsi_slave_ops,
                          s, TYPE_FSI_SLAVE, 0x400);
}

static void fsi_slave_reset(DeviceState *dev)
{
    /* FIXME */
}

static void fsi_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = fsi_slave_reset;
}

static const TypeInfo fsi_slave_info = {
    .name = TYPE_FSI_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_init = fsi_slave_init,
    .instance_size = sizeof(FSISlaveState),
    .class_init = fsi_slave_class_init,
};

static void fsi_slave_register_types(void)
{
    type_register_static(&fsi_slave_info);
}

type_init(fsi_slave_register_types);
