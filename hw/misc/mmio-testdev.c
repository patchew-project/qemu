/*
 * QEMU MMIO test device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This device is mostly used to test QEMU internal MMIO devices.
 * Accesses using CPU core are not allowed.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "hw/misc/testdev.h"
#include "hw/misc/interleaver.h"

/*
 * Device Memory Map:
 *
 *   offset       size         description
 * ----------  ----------  --------------------
 * 0x00000000  [   2 KiB]  SRAM (8 banks of 256B)
 * 0x10000000  [ 128 MiB]  interleaved-container
 * 0x11608000  [   4 KiB]  interleaved-16x8  (each device interleaves the sram)
 * 0x13208000  [   8 KiB]  interleaved-32x8   "
 * 0x13216000  [   4 KiB]  interleaved-32x16  "
 * 0x16408000  [  16 KiB]  interleaved-64x8   "
 * 0x16416000  [   8 KiB]  interleaved-64x16  "
 * 0x16432000  [   4 KiB]  interleaved-64x32  "
 * 0x20000000  [ 256 MiB]  container
 *
 * All gap regions are reserved.
 */

typedef struct MmioTestDevice {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion container;
    MemoryRegion sram;
    MemoryRegion sram_alias[8];
    MemoryRegion interleaver_container;
    MemoryRegion iomem;

    uint64_t base;
} MmioTestDevice;

#define TESTDEV(obj) \
     OBJECT_CHECK(MmioTestDevice, (obj), TYPE_MMIO_TESTDEV)

static void mmio_testdev_realize(DeviceState *dev, Error **errp)
{
    static const unsigned bhexs[] = {
        [8] = 0x8, [16] = 0x16, [32] = 0x32, [64] = 0x64,
    };
    static const struct {
        unsigned in, out;
        const char *typename;
    } cfg[] = {
        {16, 8,  TYPE_INTERLEAVER_16X8_DEVICE},
        {32, 8,  TYPE_INTERLEAVER_32X8_DEVICE},
        {32, 16, TYPE_INTERLEAVER_32X16_DEVICE},
        {64, 8,  TYPE_INTERLEAVER_64X8_DEVICE},
        {64, 16, TYPE_INTERLEAVER_64X16_DEVICE},
        {64, 32, TYPE_INTERLEAVER_64X32_DEVICE},
    };
    MmioTestDevice *s = TESTDEV(dev);
    DeviceState *interleaver;

    if (s->base == UINT64_MAX) {
        error_setg(errp, "property 'address' not specified or zero");
        return;
    }

    memory_region_init(&s->container, OBJECT(s), "testdev", 0x20000000);

    memory_region_init_ram(&s->sram, OBJECT(s), "testdev-sram",
                           0x800, &error_fatal);
    memory_region_add_subregion(&s->container, 0x000000, &s->sram);

    /* interleaved memory */
    memory_region_init(&s->interleaver_container, OBJECT(s),
                       "interleaver-container", 0x8000000);
    memory_region_add_subregion(&s->container, 0x10000000,
                                &s->interleaver_container);
    for (unsigned i = 0; i < 8; i++) {
        g_autofree char *name = g_strdup_printf("sram-p%u", i);
        /* Each alias access a 256B region of the SRAM */
        memory_region_init_alias(&s->sram_alias[i], OBJECT(s), name,
                                 &s->sram, i * 0x100, 0x100);
    }
    for (size_t i = 0; i < ARRAY_SIZE(cfg); i++) {
        unsigned count = cfg[i].in / cfg[i].out;

        interleaver = qdev_new(cfg[i].typename);
        qdev_prop_set_uint64(interleaver, "size", count * 0x100);
        /* Map 256B SRAM regions on interleaver banks */
        for (unsigned c = 0; c < count; c++) {
            g_autofree char *prop_name = g_strdup_printf("mr%u", c);
            object_property_set_link(OBJECT(interleaver), prop_name,
                                     OBJECT(&s->sram_alias[c]), &error_abort);
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(interleaver), &error_fatal);
        memory_region_add_subregion(&s->interleaver_container,
                (bhexs[cfg[i].in] << 20) | (bhexs[cfg[i].out] << 12),
                sysbus_mmio_get_region(SYS_BUS_DEVICE(interleaver), 0));
    }

    memory_region_add_subregion(get_system_memory(), s->base, &s->container);
}

static Property mmio_testdev_properties[] = {
    DEFINE_PROP_UINT64("address", MmioTestDevice, base, UINT64_MAX),
    DEFINE_PROP_END_OF_LIST(),
};

static void mmio_testdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mmio_testdev_realize;
    dc->user_creatable = true;
    device_class_set_props(dc, mmio_testdev_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo mmio_testdev_info = {
    .name           = TYPE_MMIO_TESTDEV,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(MmioTestDevice),
    .class_init     = mmio_testdev_class_init,
};

static void mmio_testdev_register_types(void)
{
    type_register_static(&mmio_testdev_info);
}

type_init(mmio_testdev_register_types)
