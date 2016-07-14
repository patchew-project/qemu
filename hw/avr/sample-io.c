/*
 *  QEMU AVR CPU
 *
 *  Copyright (c) 2016 Michael Rolnik
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

/*
    NOTE:
        This is not a real AVR device !!! This is an example !!!

        This example can be used to build a real AVR device.

        AVR has the following layout of data memory

        LD/ST(addr)                         IN/OUT(addr)
        -----------                         ------------

        0000    #-----------------------#
           .    |                       |
           .    |   32 CPU registers    |
        001f    |                       |
        0020    #-----------------------#   0000
           .    |                       |   .
           .    |   64 CPU IO registers |   .
        005f    |                       |   003f
        0060    #-----------------------#
           .    |                       |
           .    |  160 EXT IO registers |
        00ff    |                       |
        0100    #-----------------------#
                |                       |
                |  Internal RAM         |
                |                       |
                #-----------------------#
                |                       |
                |  External RAM         |
                |                       |
                #-----------------------#

        Current AVR/CPU implementation assumes that IO device responsible to
        implement functionality of IO and EXT IO registers is a memory mapped
        device, mapped to addresses in the range [0x0020 .. 0x0100)

        IN/OUT are implemented as an alias to LD/ST instructions

        Some of CPU IO registers are implemented within the CPU itself, any
        access to them either by LD/ST or IN/OUT won't be routed to the device.

*/

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "include/hw/sysbus.h"

#define TYPE_SAMPLEIO   "SampleIO"
#define SAMPLEIO(obj)   OBJECT_CHECK(SAMPLEIOState, (obj), TYPE_SAMPLEIO)

#ifndef DEBUG_SAMPLEIO
#define DEBUG_SAMPLEIO  0
#endif

#define DPRINTF(fmt, args...)                                                 \
    do {                                                                      \
        if (DEBUG_SAMPLEIO) {                                                 \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_SAMPLEIO, __func__, ##args);\
        }                                                                     \
    }                                                                         \
    while (0)

typedef struct SAMPLEIOState {
    SysBusDevice    parent;
    MemoryRegion    iomem;
} SAMPLEIOState;

static Property sample_io_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};
static const VMStateDescription  sample_io_vmstate = {
    .name = TYPE_SAMPLEIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[])
    {
        VMSTATE_END_OF_LIST()
    }
};

static
void sample_io_reset(DeviceState *dev)
{
}

static uint64_t sample_io_read(void *opaque, hwaddr offset, unsigned size)
{
    /*
        This is just an example. No real functionality is here.
    */
    qemu_log("%s addr:%2x\n", __func__, (int)offset);

    return  0;
}

static void sample_io_write(void *opaque, hwaddr offset, uint64_t value,
                                                                unsigned size)
{
    /*
        This is just an example. No real functionality is here.
    */
    qemu_log("%s addr:%2x data:%2x\n", __func__, (int)offset, (int)value);
}

static const MemoryRegionOps sample_io_ops = {
    .read = sample_io_read,
    .write = sample_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int sample_io_init(DeviceState *dev)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SAMPLEIOState *s = SAMPLEIO(dev);

    memory_region_init_io(
            &s->iomem,
            OBJECT(s),
            &sample_io_ops,
            s,
            TYPE_SAMPLEIO,
            AVR_IO_REGS);
    sysbus_init_mmio(sbd, &s->iomem);

    return  0;
}

static void sample_io_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->init = sample_io_init;
    dc->reset = sample_io_reset;
    dc->desc = "an example of AVR IO and EXT IO registers implementation";
    dc->vmsd = &sample_io_vmstate;
    dc->props = sample_io_properties;
}

static const
TypeInfo    sample_io_info = {
    .name = TYPE_SAMPLEIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SAMPLEIOState),
    .class_init = sample_io_class_init,
};

static void sample_io_register_types(void)
{
    type_register_static(&sample_io_info);
}

type_init(sample_io_register_types)

