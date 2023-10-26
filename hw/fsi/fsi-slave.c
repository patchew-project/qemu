/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface slave
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"

#include "hw/fsi/fsi-slave.h"
#include "hw/fsi/fsi.h"

#define TO_REG(x)                               ((x) >> 2)

static uint64_t fsi_slave_read(void *opaque, hwaddr addr, unsigned size)
{
    FSISlaveState *s = FSI_SLAVE(opaque);

    trace_fsi_slave_read(addr, size);

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

    trace_fsi_slave_write(addr, size, data);

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

static const TypeInfo fsi_slave_info = {
    .name = TYPE_FSI_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_init = fsi_slave_init,
    .instance_size = sizeof(FSISlaveState),
};

static void fsi_slave_register_types(void)
{
    type_register_static(&fsi_slave_info);
}

type_init(fsi_slave_register_types);
