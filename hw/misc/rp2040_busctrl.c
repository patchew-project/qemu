/*
 * RP2040 bus fabric control emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_busctrl.h"
#include "hw/misc/rp2040_nyi.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define BUSCTRL_PRIORITY        0x00
#define BUSCTRL_PRIORITY_ACK    0x04
#define BUSCTRL_PERFCTR_BASE    0x08
#define BUSCTRL_PERFSEL_BASE    0x0c
#define BUSCTRL_PERF_STRIDE     0x08
#define BUSCTRL_NUM_COUNTERS    4

#define BUSCTRL_PRIORITY_MASK   0x00001111
#define BUSCTRL_PRIORITY_ACKED  BIT(0)
#define BUSCTRL_PERFCTR_MASK    0x00ffffff
#define BUSCTRL_PERFSEL_RESET   0x1f
#define BUSCTRL_PERFSEL_MASK    0x1f

#define ATOMIC_ALIAS_MASK       0x3000
#define ATOMIC_XOR              0x1000
#define ATOMIC_SET              0x2000
#define ATOMIC_CLR              0x3000

static uint32_t rp2040_busctrl_apply_alias(uint32_t old, uint32_t value,
                                           hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static uint32_t rp2040_busctrl_read_counter(RP2040BusCtrlState *s,
                                            unsigned index)
{
    if (s->perfsel[index] != BUSCTRL_PERFSEL_RESET &&
        s->perfctr[index] != BUSCTRL_PERFCTR_MASK) {
        s->perfctr[index]++;
    }

    return s->perfctr[index];
}

static uint64_t rp2040_busctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040BusCtrlState *s = opaque;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint64_t value;

    if (offset >= BUSCTRL_PERFCTR_BASE &&
        offset < BUSCTRL_PERFCTR_BASE + BUSCTRL_NUM_COUNTERS *
        BUSCTRL_PERF_STRIDE &&
        (offset - BUSCTRL_PERFCTR_BASE) % BUSCTRL_PERF_STRIDE == 0) {
        index = (offset - BUSCTRL_PERFCTR_BASE) / BUSCTRL_PERF_STRIDE;
        return rp2040_busctrl_read_counter(s, index);
    }

    if (offset >= BUSCTRL_PERFSEL_BASE &&
        offset < BUSCTRL_PERFSEL_BASE + BUSCTRL_NUM_COUNTERS *
        BUSCTRL_PERF_STRIDE &&
        (offset - BUSCTRL_PERFSEL_BASE) % BUSCTRL_PERF_STRIDE == 0) {
        index = (offset - BUSCTRL_PERFSEL_BASE) / BUSCTRL_PERF_STRIDE;
        return s->perfsel[index];
    }

    switch (offset) {
    case BUSCTRL_PRIORITY:
        value = s->priority;
        break;
    case BUSCTRL_PRIORITY_ACK:
        value = BUSCTRL_PRIORITY_ACKED;
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("busctrl", size,
                                      RP2040_BUSCTRL_BASE + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_busctrl_write(void *opaque, hwaddr addr,
                                 uint64_t value64, unsigned size)
{
    RP2040BusCtrlState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint32_t value = value64;

    if (offset >= BUSCTRL_PERFCTR_BASE &&
        offset < BUSCTRL_PERFCTR_BASE + BUSCTRL_NUM_COUNTERS *
        BUSCTRL_PERF_STRIDE &&
        (offset - BUSCTRL_PERFCTR_BASE) % BUSCTRL_PERF_STRIDE == 0) {
        index = (offset - BUSCTRL_PERFCTR_BASE) / BUSCTRL_PERF_STRIDE;
        s->perfctr[index] =
            rp2040_busctrl_apply_alias(s->perfctr[index], value, alias) &
            BUSCTRL_PERFCTR_MASK;
        return;
    }

    if (offset >= BUSCTRL_PERFSEL_BASE &&
        offset < BUSCTRL_PERFSEL_BASE + BUSCTRL_NUM_COUNTERS *
        BUSCTRL_PERF_STRIDE &&
        (offset - BUSCTRL_PERFSEL_BASE) % BUSCTRL_PERF_STRIDE == 0) {
        index = (offset - BUSCTRL_PERFSEL_BASE) / BUSCTRL_PERF_STRIDE;
        s->perfsel[index] =
            rp2040_busctrl_apply_alias(s->perfsel[index], value, alias) &
            BUSCTRL_PERFSEL_MASK;
        return;
    }

    switch (offset) {
    case BUSCTRL_PRIORITY:
        s->priority = rp2040_busctrl_apply_alias(s->priority, value, alias) &
                      BUSCTRL_PRIORITY_MASK;
        break;
    case BUSCTRL_PRIORITY_ACK:
        break;
    default:
        rp2040_log_unimplemented_write("busctrl", size,
                                       RP2040_BUSCTRL_BASE + addr, offset,
                                       value64);
        break;
    }
}

static const MemoryRegionOps rp2040_busctrl_ops = {
    .read = rp2040_busctrl_read,
    .write = rp2040_busctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_busctrl_reset(DeviceState *dev)
{
    RP2040BusCtrlState *s = RP2040_BUSCTRL(dev);
    int i;

    s->priority = 0;
    for (i = 0; i < BUSCTRL_NUM_COUNTERS; i++) {
        s->perfctr[i] = 0;
        s->perfsel[i] = BUSCTRL_PERFSEL_RESET;
    }
}

static void rp2040_busctrl_init(Object *obj)
{
    RP2040BusCtrlState *s = RP2040_BUSCTRL(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_busctrl_ops, s,
                          "rp2040.busctrl", RP2040_BUSCTRL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_busctrl_vmstate = {
    .name = TYPE_RP2040_BUSCTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(priority, RP2040BusCtrlState),
        VMSTATE_UINT32_ARRAY(perfctr, RP2040BusCtrlState, 4),
        VMSTATE_UINT32_ARRAY(perfsel, RP2040BusCtrlState, 4),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_busctrl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_busctrl_reset);
    dc->vmsd = &rp2040_busctrl_vmstate;
}

static const TypeInfo rp2040_busctrl_info = {
    .name          = TYPE_RP2040_BUSCTRL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040BusCtrlState),
    .instance_init = rp2040_busctrl_init,
    .class_init    = rp2040_busctrl_class_init,
};

static void rp2040_busctrl_register_types(void)
{
    type_register_static(&rp2040_busctrl_info);
}
type_init(rp2040_busctrl_register_types)
