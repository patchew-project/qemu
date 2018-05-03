/*
 * Nordic Semiconductor nRF51 SoC
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/arm.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/devices.h"
#include "hw/misc/unimp.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "cpu.h"

#include "hw/arm/nrf51_soc.h"

#define IOMEM_BASE      0x40000000
#define IOMEM_SIZE      0x20000000

#define FLASH_BASE      0x00000000
#define FLASH_SIZE      (144 * 1024)

#define SRAM_BASE       0x20000000
#define SRAM_SIZE       (6 * 1024)

static void nrf51_soc_realize(DeviceState *dev_soc, Error **errp)
{
    NRF51State *s = NRF51_SOC(dev_soc);
    DeviceState *nvic;
    Error *err = NULL;

    /* IO space */
    create_unimplemented_device("nrf51_soc.io", IOMEM_BASE, IOMEM_SIZE);

    /* FICR */
    create_unimplemented_device("nrf51_soc.ficr", FICR_BASE, FICR_SIZE);

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);

    memory_region_init_ram_nomigrate(flash, NULL, "nrf51.flash", FLASH_SIZE,
            &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    vmstate_register_ram_global(flash);
    memory_region_set_readonly(flash, true);

    memory_region_add_subregion(system_memory, FLASH_BASE, flash);

    memory_region_init_ram_nomigrate(sram, NULL, "nrf51.sram", SRAM_SIZE,
            &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(system_memory, SRAM_BASE, sram);

    /* TODO: implement a cortex m0 and update this */
    nvic = armv7m_init(get_system_memory(), FLASH_SIZE, 96,
            s->kernel_filename, ARM_CPU_TYPE_NAME("cortex-m3"));
}

static Property nrf51_soc_properties[] = {
    DEFINE_PROP_STRING("kernel-filename", NRF51State, kernel_filename),
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf51_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nrf51_soc_realize;
    dc->props = nrf51_soc_properties;
}

static const TypeInfo nrf51_soc_info = {
    .name          = TYPE_NRF51_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF51State),
    .class_init    = nrf51_soc_class_init,
};

static void nrf51_soc_types(void)
{
    type_register_static(&nrf51_soc_info);
}
type_init(nrf51_soc_types)

