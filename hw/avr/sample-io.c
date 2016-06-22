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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "include/hw/sysbus.h"

#define TYPE_SAMPLEIO   "SampleIO"
#define SAMPLEIO(obj)   OBJECT_CHECK(SAMPLEIOState, (obj), TYPE_SAMPLEIO)

#ifndef DEBUG_SAMPLEIO
#define DEBUG_SAMPLEIO 1
#endif

#define DPRINTF(fmt, args...)                                                 \
    do {                                                                      \
        if (DEBUG_SAMPLEIO) {                                                 \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_SAMPLEIO, __func__, ##args);\
        }                                                                     \
    }                                                                         \
    while (0)

#define AVR_IO_CPU_REGS_SIZE    0x0020
#define AVR_IO_CPU_IO_SIZE      0x0040
#define AVR_IO_EXT_IO_SIZE   0x00a0
#define AVR_IO_SIZE             (AVR_IO_CPU_REGS_SIZE   \
                                + AVR_IO_CPU_IO_SIZE    \
                                + AVR_IO_EXT_IO_SIZE)

#define AVR_IO_CPU_REGS_BASE    0x0000
#define AVR_IO_CPU_IO_BASE      (AVR_IO_CPU_REGS_BASE   \
                                + AVR_IO_CPU_REGS_SIZE)
#define AVR_IO_EXTERN_IO_BASE   (AVR_IO_CPU_IO_BASE     \
                                + AVR_IO_CPU_IO_SIZE)


typedef struct SAMPLEIOState {
    SysBusDevice    parent;

    MemoryRegion    iomem;

    AVRCPU         *cpu;

    uint8_t         io[AVR_IO_CPU_IO_SIZE];
    uint8_t         exio[AVR_IO_EXT_IO_SIZE];
} SAMPLEIOState;

static uint64_t sample_io_read(void *opaque, hwaddr offset, unsigned size);
static void sample_io_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size);
static int sample_io_init(DeviceState *sbd);
static void sample_io_class_init(ObjectClass *klass, void *data);
static void sample_io_register_types(void);

static void write_Rx(CPUAVRState *env, int inst, uint8_t data);
static uint8_t read_Rx(CPUAVRState *env, int inst);

static const MemoryRegionOps sample_io_ops = {
    .read = sample_io_read,
    .write = sample_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

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

void write_Rx(CPUAVRState *env, int inst, uint8_t data)
{
    env->r[inst] = data;
}
uint8_t     read_Rx(CPUAVRState *env, int inst)
{
    return  env->r[inst];
}

static
void sample_io_reset(DeviceState *dev)
{
    DPRINTF("\n");
}

static
uint64_t    sample_io_read(void *opaque, hwaddr offset, unsigned size)
{
    SAMPLEIOState *s = SAMPLEIO(opaque);
    AVRCPU *cpu = s->cpu;
    CPUAVRState *env = &cpu->env;
    uint64_t res = 0;

    assert(size == 1);

#if AVR_IO_CPU_REGS_BASE == 0
    if (offset < (AVR_IO_CPU_REGS_BASE + AVR_IO_CPU_REGS_SIZE)) {
#else
    if (AVR_IO_CPU_REGS_BASE <= offset
        && offset < (AVR_IO_CPU_REGS_BASE + AVR_IO_CPU_REGS_SIZE)) {
#endif
        res = read_Rx(env, offset - AVR_IO_CPU_REGS_BASE);
    } else if (AVR_IO_CPU_IO_BASE <= offset
            && offset < (AVR_IO_CPU_IO_BASE + AVR_IO_CPU_IO_SIZE)) {
        /*  TODO: do IO related stuff here  */
        res = s->io[offset - AVR_IO_CPU_IO_BASE];
    } else if (AVR_IO_EXTERN_IO_BASE <= offset
            && offset < (AVR_IO_EXTERN_IO_BASE + AVR_IO_EXT_IO_SIZE)) {
        /*  TODO: do EXT IO related stuff here  */
        res = s->io[offset - AVR_IO_EXTERN_IO_BASE];
    } else {
        g_assert_not_reached();
    }

    qemu_log("%s addr:%2x data:%2x\n", __func__, (int)offset, (int)res);

    return  res;
}

static
void sample_io_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    SAMPLEIOState *s = SAMPLEIO(opaque);
    AVRCPU *cpu = s->cpu;
    CPUAVRState *env = &cpu->env;

    assert(size == 1);

    qemu_log("%s addr:%2x data:%2x\n", __func__, (int)offset, (int)value);

#if AVR_IO_CPU_REGS_BASE == 0
    if (offset < (AVR_IO_CPU_REGS_BASE + AVR_IO_CPU_REGS_SIZE)) {
#else
    if (AVR_IO_CPU_REGS_BASE <= offset
        && offset < (AVR_IO_CPU_REGS_BASE + AVR_IO_CPU_REGS_SIZE)) {
#endif
        return  write_Rx(env, offset - AVR_IO_CPU_REGS_BASE, value);
    } else if (AVR_IO_CPU_IO_BASE <= offset
            && offset < (AVR_IO_CPU_IO_BASE + AVR_IO_CPU_IO_SIZE)) {
        /*  TODO: do IO related stuff here  */
        s->io[offset - AVR_IO_CPU_IO_BASE] = value;
    } else if (AVR_IO_EXTERN_IO_BASE <= offset
            && offset < (AVR_IO_EXTERN_IO_BASE + AVR_IO_EXT_IO_SIZE)) {
        /*  TODO: do EXT IO related stuff here  */
        s->io[offset - AVR_IO_EXTERN_IO_BASE] = value;
    } else {
        g_assert_not_reached();
    }
}

static
int         sample_io_init(DeviceState *dev)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SAMPLEIOState *s = SAMPLEIO(dev);

    assert(AVR_IO_SIZE <= TARGET_PAGE_SIZE);

    s->cpu = AVR_CPU(qemu_get_cpu(0));

    memory_region_init_io(
            &s->iomem,
            OBJECT(s),
            &sample_io_ops,
            s,
            TYPE_SAMPLEIO,
            AVR_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    return  0;
}

static
void sample_io_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    DPRINTF("\n");

    dc->init = sample_io_init;
    dc->reset = sample_io_reset;
    dc->desc = "at90 io regs";
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

static
void sample_io_register_types(void)
{
    DPRINTF("\n");
    type_register_static(&sample_io_info);
}

type_init(sample_io_register_types)
