/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/arm/armv7m.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "target/arm/idau.h"

/* Bitbanded IO.  Each word corresponds to a single bit.  */

/* Get the byte address of the real memory for a bitband access.  */
static inline hwaddr bitband_addr(BitBandState *s, hwaddr offset)
{
    return s->base | (offset & 0x1ffffff) >> 5;
}

static MemTxResult bitband_read(void *opaque, hwaddr offset,
                                uint64_t *data, unsigned size, MemTxAttrs attrs)
{
    BitBandState *s = opaque;
    uint8_t buf[4];
    MemTxResult res;
    int bitpos, bit;
    hwaddr addr;

    assert(size <= 4);

    /* Find address in underlying memory and round down to multiple of size */
    addr = bitband_addr(s, offset) & (-size);
    res = address_space_read(&s->source_as, addr, attrs, buf, size);
    if (res) {
        return res;
    }
    /* Bit position in the N bytes read... */
    bitpos = (offset >> 2) & ((size * 8) - 1);
    /* ...converted to byte in buffer and bit in byte */
    bit = (buf[bitpos >> 3] >> (bitpos & 7)) & 1;
    *data = bit;
    return MEMTX_OK;
}

static MemTxResult bitband_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    BitBandState *s = opaque;
    uint8_t buf[4];
    MemTxResult res;
    int bitpos, bit;
    hwaddr addr;

    assert(size <= 4);

    /* Find address in underlying memory and round down to multiple of size */
    addr = bitband_addr(s, offset) & (-size);
    res = address_space_read(&s->source_as, addr, attrs, buf, size);
    if (res) {
        return res;
    }
    /* Bit position in the N bytes read... */
    bitpos = (offset >> 2) & ((size * 8) - 1);
    /* ...converted to byte in buffer and bit in byte */
    bit = 1 << (bitpos & 7);
    if (value & 1) {
        buf[bitpos >> 3] |= bit;
    } else {
        buf[bitpos >> 3] &= ~bit;
    }
    return address_space_write(&s->source_as, addr, attrs, buf, size);
}

static const MemoryRegionOps bitband_ops = {
    .read_with_attrs = bitband_read,
    .write_with_attrs = bitband_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void bitband_init(Object *obj)
{
    BitBandState *s = BITBAND(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bitband_ops, s,
                          "bitband", 0x02000000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void bitband_realize(DeviceState *dev, Error **errp)
{
    BitBandState *s = BITBAND(dev);

    if (!s->source_memory) {
        error_setg(errp, "source-memory property not set");
        return;
    }

    address_space_init(&s->source_as, s->source_memory, "bitband-source");
}

/* Board init.  */

static const hwaddr bitband_input_addr[ARMV7M_NUM_BITBANDS] = {
    0x20000000, 0x40000000
};

static const hwaddr bitband_output_addr[ARMV7M_NUM_BITBANDS] = {
    0x22000000, 0x42000000
};

static void armv7m_instance_init(Object *obj)
{
    ARMv7MState *s = ARMV7M(obj);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        object_initialize(&s->bitband[i], sizeof(s->bitband[i]), TYPE_BITBAND);
        qdev_set_parent_bus(DEVICE(&s->bitband[i]), sysbus_get_default());
    }
}

static void armv7m_cpu_init(ARMMProfileState *s_, Error **errp)
{
    ARMv7MState *s = ARMV7M(s_);
    ARMCPU *cpu = s_->cpu;
    Error *err = NULL;

    if (object_property_find(OBJECT(cpu), "idau", NULL)) {
        object_property_set_link(OBJECT(cpu), s->idau, "idau", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }
    if (object_property_find(OBJECT(cpu), "init-svtor", NULL)) {
        object_property_set_uint(OBJECT(cpu), s->init_svtor,
                                 "init-svtor", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }
}

static void armv7m_realize(DeviceState *dev, Error **errp)
{
    ARMv7MState *s = ARMV7M(dev);
    ObjectClass *klass = object_get_class(OBJECT(dev));
    ObjectClass *parent_class = object_class_get_parent(klass);
    DeviceRealize parent_realize = DEVICE_CLASS(parent_class)->realize;
    Error *err = NULL;
    int i;

    parent_realize(dev, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        Object *obj = OBJECT(&s->bitband[i]);
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->bitband[i]);

        object_property_set_int(obj, bitband_input_addr[i], "base", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_link(obj, OBJECT(s->parent_obj.board_memory),
                                 "source-memory", &error_abort);
        object_property_set_bool(obj, true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        memory_region_add_subregion(&s->parent_obj.container,
                                    bitband_output_addr[i],
                                    sysbus_mmio_get_region(sbd, 0));
    }
}

static Property armv7m_properties[] = {
    DEFINE_PROP_LINK("idau", ARMv7MState, idau, TYPE_IDAU_INTERFACE, Object *),
    DEFINE_PROP_UINT32("init-svtor", ARMv7MState, init_svtor, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void armv7m_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMMProfileClass *mc = ARM_M_PROFILE_CLASS(klass);

    dc->realize = armv7m_realize;
    dc->props = armv7m_properties;
    mc->cpu_init = armv7m_cpu_init;
}

static const TypeInfo armv7m_info = {
    .name = TYPE_ARMV7M,
    .parent = TYPE_ARM_M_PROFILE,
    .instance_size = sizeof(ARMv7MState),
    .instance_init = armv7m_instance_init,
    .class_init = armv7m_class_init,
};

static Property bitband_properties[] = {
    DEFINE_PROP_UINT32("base", BitBandState, base, 0),
    DEFINE_PROP_LINK("source-memory", BitBandState, source_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void bitband_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bitband_realize;
    dc->props = bitband_properties;
}

static const TypeInfo bitband_info = {
    .name          = TYPE_BITBAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BitBandState),
    .instance_init = bitband_init,
    .class_init    = bitband_class_init,
};

static void armv7m_register_types(void)
{
    type_register_static(&bitband_info);
    type_register_static(&armv7m_info);
}

type_init(armv7m_register_types)
