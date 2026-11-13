/*
 * Radxa ROCK 5B Board emulation
 *
 * Copyright (C) 2026 Process Mission
 *
 * Authors:
 *    Chao Liu <chao.liu@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/rk3588.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/ssi/ssi.h"
#include "system/address-spaces.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "system/device_tree.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "system/system.h"

#define TYPE_ROCK5B_MACHINE MACHINE_TYPE_NAME("rock-5b")
OBJECT_DECLARE_SIMPLE_TYPE(Rock5BMachineState, ROCK5B_MACHINE)

enum {
    ROCK5B_LOW_RAM,
    ROCK5B_HIGH_RAM,
};

static const MemMapEntry rock5b_memmap[] = {
    [ROCK5B_LOW_RAM]  = { 0x000000000, 0x0f0000000 },
    [ROCK5B_HIGH_RAM] = { 0x100000000, 0x310000000 },
};

#define ROCK5B_FLASH_SIZE (16 * MiB)
/* Match the low-memory reservation made by Rockchip U-Boot for BL31. */
#define ROCK5B_FIRMWARE_RESERVED_SIZE (2 * MiB)
/* UART2 drives the board's debug header. */
#define ROCK5B_CONSOLE_UART 2

struct Rock5BMachineState {
    MachineState parent_obj;

    RK3588SoCState soc;
    MemoryRegion low_ram;
    MemoryRegion high_ram;
    uint64_t low_ram_size;
    uint64_t high_ram_size;
    struct arm_boot_info binfo;
};

/*
 * The first -serial option drives the debug console; further options
 * connect to the remaining UARTs in index order.
 */
static void rock5b_connect_serial(Rock5BMachineState *s)
{
    unsigned int hd = 1;

    /* UART0 in the PMU power domain is not modeled. */
    for (unsigned int i = 1; i < RK3588_NUM_UARTS; i++) {
        g_autofree char *prop = g_strdup_printf("uart%u-chardev", i);

        qdev_prop_set_chr(DEVICE(&s->soc), prop,
                          serial_hd(i == ROCK5B_CONSOLE_UART ? 0 : hd++));
    }
}

static void rock5b_create_flash(Rock5BMachineState *s)
{
    DriveInfo *di = drive_get(IF_MTD, 0, 0);
    DeviceState *flash = qdev_new("xt25f128");

    object_property_add_child(OBJECT(s), "spi-nor", OBJECT(flash));
    if (di) {
        BlockBackend *blk = blk_by_legacy_dinfo(di);
        int64_t length = blk_getlength(blk);

        if (length != ROCK5B_FLASH_SIZE) {
            error_report("rock-5b: SPI NOR image must be exactly 16 MiB");
            exit(EXIT_FAILURE);
        }
        qdev_prop_set_drive_err(flash, "drive", blk, &error_fatal);
    }
    qdev_realize_and_unref(flash, qdev_get_child_bus(s->soc.sfc, "spi"),
                           &error_fatal);
    qdev_connect_gpio_out_named(s->soc.sfc, "cs", 0,
                                qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0));
}

/*
 * arm_load_dtb() replaces the memory nodes with a single node for low RAM;
 * add the RAM above the peripheral hole.
 */
static void rock5b_modify_dtb(const struct arm_boot_info *binfo, void *fdt)
{
    const Rock5BMachineState *s = container_of(binfo, Rock5BMachineState,
                                               binfo);
    hwaddr base = rock5b_memmap[ROCK5B_HIGH_RAM].base;
    g_autofree char *node = NULL;
    uint32_t acells, scells;

    if (!s->high_ram_size) {
        return;
    }

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells", NULL,
                                   &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells", NULL,
                                   &error_fatal);
    node = g_strdup_printf("/memory@%" HWADDR_PRIx, base);
    qemu_fdt_add_subnode(fdt, node);
    qemu_fdt_setprop_string(fdt, node, "device_type", "memory");
    if (qemu_fdt_setprop_sized_cells(fdt, node, "reg", acells, base,
                                     scells, s->high_ram_size) < 0) {
        error_report("rock-5b: device tree cells cannot describe RAM "
                     "above 4 GiB");
        exit(EXIT_FAILURE);
    }
}

/*
 * arm_load_kernel() enters Linux at EL2, keeps the SMC PSCI conduit and
 * registers the CPU reset handlers.
 */
static void rock5b_direct_boot(Rock5BMachineState *s)
{
    s->binfo.ram_size = s->low_ram_size - ROCK5B_FIRMWARE_RESERVED_SIZE;
    s->binfo.loader_start = ROCK5B_FIRMWARE_RESERVED_SIZE;
    s->binfo.board_id = -1;
    s->binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    s->binfo.modify_dtb = rock5b_modify_dtb;
    arm_load_kernel(&s->soc.cpu[0], MACHINE(s), &s->binfo);
}

static void rock5b_init(MachineState *ms)
{
    Rock5BMachineState *s = ROCK5B_MACHINE(ms);

    if (ms->ram_size < 128 * MiB || ms->ram_size > 16 * GiB) {
        error_report("rock-5b: memory size must be between "
                     "128 MiB and 16 GiB");
        exit(EXIT_FAILURE);
    }

    if (kvm_enabled()) {
        error_report("rock-5b: KVM is not supported");
        exit(EXIT_FAILURE);
    }

    if (ms->firmware) {
        error_report("rock-5b: -bios is not supported");
        exit(EXIT_FAILURE);
    }
    if (ms->kernel_filename) {
        if (!ms->dtb) {
            error_report("rock-5b: direct Linux boot requires -dtb");
            exit(EXIT_FAILURE);
        }
    } else if (!qtest_enabled()) {
        error_report("rock-5b: supply a Linux kernel with -kernel");
        exit(EXIT_FAILURE);
    }

    /* Initialize the SoC object. */
    object_initialize_child(OBJECT(s), "soc", &s->soc, TYPE_RK3588_SOC);
    object_property_set_link(OBJECT(&s->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    rock5b_connect_serial(s);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    s->low_ram_size = MIN(ms->ram_size,
                          rock5b_memmap[ROCK5B_LOW_RAM].size);
    s->high_ram_size = ms->ram_size - s->low_ram_size;
    memory_region_init_alias(&s->low_ram, OBJECT(s), "rock-5b.low-ram",
                             ms->ram, 0, s->low_ram_size);
    memory_region_add_subregion(get_system_memory(),
                                rock5b_memmap[ROCK5B_LOW_RAM].base,
                                &s->low_ram);
    if (s->high_ram_size) {
        memory_region_init_alias(&s->high_ram, OBJECT(s),
                                 "rock-5b.high-ram", ms->ram,
                                 s->low_ram_size, s->high_ram_size);
        memory_region_add_subregion(get_system_memory(),
                                    rock5b_memmap[ROCK5B_HIGH_RAM].base,
                                    &s->high_ram);
    }

    rock5b_create_flash(s);

    rock5b_direct_boot(s);
}

static void rock5b_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Radxa ROCK 5B";
    mc->init = rock5b_init;
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "rock-5b.ram";
    mc->min_cpus = RK3588_NUM_CPUS;
    mc->default_cpus = RK3588_NUM_CPUS;
    mc->max_cpus = RK3588_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a55");
}

static const TypeInfo rock5b_typeinfo = {
    .name = TYPE_ROCK5B_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Rock5BMachineState),
    .class_init = rock5b_class_init,
};

static void rock5b_register_types(void)
{
    type_register_static(&rock5b_typeinfo);
}

type_init(rock5b_register_types)
