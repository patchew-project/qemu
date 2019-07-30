/*
 * Data Scratch Pad RAM
 *
 * Copyright (c) 2017 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/log.h"
#include "exec/exec-all.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/misc/mips_dspram.h"

static void raise_exception(int excp)
{
    current_cpu->exception_index = excp;
    cpu_loop_exit(current_cpu);
}

static uint64_t dspram_read(void *opaque, hwaddr addr, unsigned size)
{
    MIPSDSPRAMState *s = (MIPSDSPRAMState *)opaque;

    switch (size) {
    case 1:
    case 2:
        raise_exception(EXCP_AdEL);
        return 0;
    case 4:
        return *(uint32_t *) &s->ramblock[addr % (1 << s->size)];
    case 8:
        return *(uint64_t *) &s->ramblock[addr % (1 << s->size)];
    }
    return 0;
}

static void dspram_write(void *opaque, hwaddr addr, uint64_t data,
                         unsigned size)
{
    MIPSDSPRAMState *s = (MIPSDSPRAMState *)opaque;

    switch (size) {
    case 1:
    case 2:
        raise_exception(EXCP_AdES);
        return;
    case 4:
        *(uint32_t *) &s->ramblock[addr % (1 << s->size)] = (uint32_t) data;
        break;
    case 8:
        *(uint64_t *) &s->ramblock[addr % (1 << s->size)] = data;
        break;
    }
}

void dspram_reconfigure(struct MIPSDSPRAMState *dspram)
{
    MemoryRegion *mr = &dspram->mr;
    hwaddr address;
    bool is_enabled;

    address = ((*(uint64_t *) dspram->saar) & 0xFFFFFFFE000ULL) << 4;
    is_enabled = *(uint64_t *) dspram->saar & 1;

    memory_region_transaction_begin();
    memory_region_set_size(mr, (1 << dspram->size));
    memory_region_set_address(mr, address);
    memory_region_set_enabled(mr, is_enabled);
    memory_region_transaction_commit();
}

static const MemoryRegionOps dspram_ops = {
    .read = dspram_read,
    .write = dspram_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .unaligned = false,
    }
};

static void mips_dspram_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSDSPRAMState *s = MIPS_DSPRAM(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &dspram_ops, s,
                          "mips-dspram", (1 << s->size));
    sysbus_init_mmio(sbd, &s->mr);
}

static void mips_dspram_realize(DeviceState *dev, Error **errp)
{
    MIPSDSPRAMState *s = MIPS_DSPRAM(dev);

    /* some error handling here */

    s->ramblock = g_malloc0(1 << s->size);
}

static void mips_dspram_reset(DeviceState *dev)
{
    MIPSDSPRAMState *s = MIPS_DSPRAM(dev);

    *(uint64_t *) s->saar = s->size << 1;
    memset(s->ramblock, 0, (1 << s->size));
}

static Property mips_dspram_properties[] = {
    DEFINE_PROP_PTR("saar", MIPSDSPRAMState, saar),
    /* default DSPRAM size is 64 KB */
    DEFINE_PROP_SIZE("size", MIPSDSPRAMState, size, 0x10),
    DEFINE_PROP_END_OF_LIST(),
};

static void mips_dspram_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = mips_dspram_properties;
    dc->realize = mips_dspram_realize;
    dc->reset = mips_dspram_reset;
}

static const TypeInfo mips_dspram_info = {
    .name          = TYPE_MIPS_DSPRAM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSDSPRAMState),
    .instance_init = mips_dspram_init,
    .class_init    = mips_dspram_class_init,
};

static void mips_dspram_register_types(void)
{
    type_register_static(&mips_dspram_info);
}

type_init(mips_dspram_register_types);
