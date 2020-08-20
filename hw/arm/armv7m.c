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
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "elf.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
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

    /* Can't init the cpu here, we don't yet know which model to use */

    memory_region_init(&s->container, obj, "armv7m-container", UINT64_MAX);

    object_initialize_child(obj, "nvnic", &s->nvic, TYPE_NVIC);
    object_property_add_alias(obj, "num-irq",
                              OBJECT(&s->nvic), "num-irq");

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        object_initialize_child(obj, "bitband[*]", &s->bitband[i],
                                TYPE_BITBAND);
    }
}

static void armv7m_realize(DeviceState *dev, Error **errp)
{
    ARMv7MState *s = ARMV7M(dev);
    SysBusDevice *sbd;
    Error *err = NULL;
    int i;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    s->cpu = ARM_CPU(object_new_with_props(s->cpu_type, OBJECT(s), "cpu",
                                           &err, NULL));
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_link(OBJECT(s->cpu), "memory", OBJECT(&s->container),
                             &error_abort);
    if (object_property_find(OBJECT(s->cpu), "idau", NULL)) {
        object_property_set_link(OBJECT(s->cpu), "idau", s->idau,
                                 &error_abort);
    }
    if (object_property_find(OBJECT(s->cpu), "init-svtor", NULL)) {
        if (!object_property_set_uint(OBJECT(s->cpu), "init-svtor",
                                      s->init_svtor, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "start-powered-off", NULL)) {
        if (!object_property_set_bool(OBJECT(s->cpu), "start-powered-off",
                                      s->start_powered_off, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "vfp", NULL)) {
        if (!object_property_set_bool(OBJECT(s->cpu), "vfp", s->vfp, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "dsp", NULL)) {
        if (!object_property_set_bool(OBJECT(s->cpu), "dsp", s->dsp, errp)) {
            return;
        }
    }

    /*
     * Tell the CPU where the NVIC is; it will fail realize if it doesn't
     * have one. Similarly, tell the NVIC where its CPU is.
     */
    s->cpu->env.nvic = &s->nvic;
    s->nvic.cpu = s->cpu;

    if (!qdev_realize(DEVICE(s->cpu), NULL, errp)) {
        return;
    }

    /* Note that we must realize the NVIC after the CPU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nvic), errp)) {
        return;
    }

    /* Alias the NVIC's input and output GPIOs as our own so the board
     * code can wire them up. (We do this in realize because the
     * NVIC doesn't create the input GPIO array until realize.)
     */
    qdev_pass_gpios(DEVICE(&s->nvic), dev, NULL);
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "SYSRESETREQ");
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "NMI");

    /* Wire the NVIC up to the CPU */
    sbd = SYS_BUS_DEVICE(&s->nvic);
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));

    memory_region_add_subregion(&s->container, 0xe000e000,
                                sysbus_mmio_get_region(sbd, 0));

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        if (s->enable_bitband) {
            Object *obj = OBJECT(&s->bitband[i]);
            SysBusDevice *sbd = SYS_BUS_DEVICE(&s->bitband[i]);

            if (!object_property_set_int(obj, "base",
                                         bitband_input_addr[i], errp)) {
                return;
            }
            object_property_set_link(obj, "source-memory",
                                     OBJECT(s->board_memory), &error_abort);
            if (!sysbus_realize(SYS_BUS_DEVICE(obj), errp)) {
                return;
            }

            memory_region_add_subregion(&s->container, bitband_output_addr[i],
                                        sysbus_mmio_get_region(sbd, 0));
        } else {
            object_unparent(OBJECT(&s->bitband[i]));
        }
    }
}

static Property armv7m_properties[] = {
    DEFINE_PROP_STRING("cpu-type", ARMv7MState, cpu_type),
    DEFINE_PROP_LINK("memory", ARMv7MState, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("idau", ARMv7MState, idau, TYPE_IDAU_INTERFACE, Object *),
    DEFINE_PROP_UINT32("init-svtor", ARMv7MState, init_svtor, 0),
    DEFINE_PROP_BOOL("enable-bitband", ARMv7MState, enable_bitband, false),
    DEFINE_PROP_BOOL("start-powered-off", ARMv7MState, start_powered_off,
                     false),
    DEFINE_PROP_BOOL("vfp", ARMv7MState, vfp, true),
    DEFINE_PROP_BOOL("dsp", ARMv7MState, dsp, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void armv7m_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = armv7m_realize;
    device_class_set_props(dc, armv7m_properties);
}

static const TypeInfo armv7m_info = {
    .name = TYPE_ARMV7M,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMv7MState),
    .instance_init = armv7m_instance_init,
    .class_init = armv7m_class_init,
};
TYPE_INFO(armv7m_info)

static void armv7m_reset(void *opaque)
{
    ARMCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename, int mem_size)
{
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int big_endian;
    AddressSpace *as;
    int asidx;
    CPUState *cs = CPU(cpu);

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (arm_feature(&cpu->env, ARM_FEATURE_EL3)) {
        asidx = ARMASIdx_S;
    } else {
        asidx = ARMASIdx_NS;
    }
    as = cpu_get_address_space(cs, asidx);

    if (kernel_filename) {
        image_size = load_elf_as(kernel_filename, NULL, NULL, NULL,
                                 &entry, &lowaddr, NULL,
                                 NULL, big_endian, EM_ARM, 1, 0, as);
        if (image_size < 0) {
            image_size = load_image_targphys_as(kernel_filename, 0,
                                                mem_size, as);
            lowaddr = 0;
        }
        if (image_size < 0) {
            error_report("Could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    /* CPU objects (unlike devices) are not automatically reset on system
     * reset, so we must always register a handler to do so. Unlike
     * A-profile CPUs, we don't need to do anything special in the
     * handler to arrange that it starts correctly.
     * This is arguably the wrong place to do this, but it matches the
     * way A-profile does it. Note that this means that every M profile
     * board must call this function!
     */
    qemu_register_reset(armv7m_reset, cpu);
}

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
    device_class_set_props(dc, bitband_properties);
}

static const TypeInfo bitband_info = {
    .name          = TYPE_BITBAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BitBandState),
    .instance_init = bitband_init,
    .class_init    = bitband_class_init,
};
TYPE_INFO(bitband_info)


