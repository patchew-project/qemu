/*
 * djMEMC, macintosh memory and interrupt controller
 * (Quadra 610/650/800 & Centris 610/650)
 *
 *    https://mac68k.info/wiki/display/mac68k/djMEMC+Information
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
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/misc/djmemc.h"
#include "hw/qdev-properties.h"
#include "trace.h"


#define DJMEMC_INTERLEAVECONF   0x0
#define DJMEMC_BANK0CONF        0x4
#define DJMEMC_BANK1CONF        0x8
#define DJMEMC_BANK2CONF        0xc
#define DJMEMC_BANK3CONF        0x10
#define DJMEMC_BANK4CONF        0x14
#define DJMEMC_BANK5CONF        0x18
#define DJMEMC_BANK6CONF        0x1c
#define DJMEMC_BANK7CONF        0x20
#define DJMEMC_BANK8CONF        0x24
#define DJMEMC_BANK9CONF        0x28
#define DJMEMC_MEMTOP           0x2c
#define DJMEMC_CONFIG           0x30
#define DJMEMC_REFRESH          0x34


static uint64_t djmemc_read(void *opaque, hwaddr addr, unsigned size)
{
    DJMEMCState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DJMEMC_INTERLEAVECONF:
    case DJMEMC_BANK0CONF ... DJMEMC_BANK9CONF:
    case DJMEMC_MEMTOP:
    case DJMEMC_CONFIG:
    case DJMEMC_REFRESH:
        val = s->regs[addr >> 2];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "djMEMC: unimplemented read addr=0x%"PRIx64
                                 " val=0x%"PRIx64 " size=%d\n",
                                 addr, val, size);
    }

    trace_djmemc_read(addr, size, val);
    return val;
}

static void djmemc_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    DJMEMCState *s = opaque;

    trace_djmemc_write(addr, size, val);

    switch (addr) {
    case DJMEMC_INTERLEAVECONF:
    case DJMEMC_BANK0CONF ... DJMEMC_BANK9CONF:
    case DJMEMC_MEMTOP:
    case DJMEMC_CONFIG:
    case DJMEMC_REFRESH:
        s->regs[addr >> 2] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "djMEMC: unimplemented write addr=0x%"PRIx64
                                 " val=0x%"PRIx64 " size=%d\n",
                                 addr, val, size);
    }
}

static const MemoryRegionOps djmemc_mmio_ops = {
    .read = djmemc_read,
    .write = djmemc_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_BIG_ENDIAN,
};

static void djmemc_init(Object *obj)
{
    DJMEMCState *s = DJMEMC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mem_regs, obj, &djmemc_mmio_ops, s, "djMEMC",
                          DJMEMC_SIZE);
    sysbus_init_mmio(sbd, &s->mem_regs);
}

static void djmemc_reset_hold(Object *obj)
{
    DJMEMCState *s = DJMEMC(obj);

    memset(s->regs, 0, sizeof(s->regs));
}

static const VMStateDescription vmstate_djmemc = {
    .name = "djMEMC",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, DJMEMCState, DJMEMC_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void djmemc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.hold = djmemc_reset_hold;
    dc->vmsd = &vmstate_djmemc;
}

static const TypeInfo djmemc_info = {
    .name          = TYPE_DJMEMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DJMEMCState),
    .instance_init = djmemc_init,
    .class_init    = djmemc_class_init,
};

static void djmemc_register_types(void)
{
    type_register_static(&djmemc_info);
}

type_init(djmemc_register_types)
