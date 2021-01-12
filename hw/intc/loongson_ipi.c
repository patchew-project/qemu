/*
 * QEMU Loongson Inter Processor Interrupt Controller
 *
 * Copyright (c) 2020-2021 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/intc/loongson_ipi.h"

#define R_ISR       0
#define R_IEN       1
#define R_SET       2
#define R_CLR       3
/* No register between 0x10~0x20 */
#define R_MBOX0     8
#define NUM_MBOX    8
#define R_END       16

struct loongson_ipi {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq parent_irq;

    uint32_t isr;
    uint32_t ien;
    uint32_t mbox[NUM_MBOX];
};

static uint64_t
ipi_read(void *opaque, hwaddr addr, unsigned int size)
{
    struct loongson_ipi *p = opaque;
    uint64_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_ISR:
        r = p->isr;
        break;
    case R_IEN:
        r = p->ien;
        break;
    case R_MBOX0 ... (R_END - 1):
        r = p->mbox[addr - R_MBOX0];
        break;
    default:
        break;
    }

    qemu_log_mask(CPU_LOG_INT,
                  "%s: size=%d, addr=%"HWADDR_PRIx", val=%"PRIx64"\n",
                  __func__, size, addr, r);

    return r;
}

static void
ipi_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    struct loongson_ipi *p = opaque;
    uint32_t value = val64;

    addr >>= 2;
    switch (addr) {
    case R_ISR:
        /* Do nothing */
        break;
    case R_IEN:
        p->ien = value;
        break;
    case R_SET:
        p->isr |= value;
        break;
    case R_CLR:
        p->isr &= ~value;
        break;
    case R_MBOX0 ... (R_END - 1):
        p->mbox[addr - R_MBOX0] = value;
        break;
    default:
        break;
    }
    p->isr &= p->ien;

    qemu_log_mask(CPU_LOG_INT,
                  "%s: size=%d, addr=%"HWADDR_PRIx", val=%"PRIx32"\n",
                  __func__, size, addr, value);

    qemu_set_irq(p->parent_irq, !!p->isr);
}

static const MemoryRegionOps pic_ops = {
    .read = ipi_read,
    .write = ipi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void loongson_ipi_init(Object *obj)
{
    struct loongson_ipi *p = LOONGSON_IPI(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &p->parent_irq);

    memory_region_init_io(&p->mmio, obj, &pic_ops, p, "loongson.ipi",
                          R_END * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &p->mmio);
}

static const TypeInfo loongson_ipi_info = {
    .name          = TYPE_LOONGSON_IPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct loongson_ipi),
    .instance_init = loongson_ipi_init,
};

static void loongson_ipi_register_types(void)
{
    type_register_static(&loongson_ipi_info);
}

type_init(loongson_ipi_register_types)
