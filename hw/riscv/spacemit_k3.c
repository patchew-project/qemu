/*
 * Spacemit K3 SPI Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#include "qemu/osdep.h"

#include "qemu/units.h"

#include "hw/core/boards.h"
#include "hw/core/sysbus.h"

#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"

#include "hw/riscv/boot.h"
#include "hw/riscv/riscv_hart.h"

#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/misc/unimp.h"
#include "hw/sd/sd.h"
#include "system/blockdev.h"
#include "hw/ssi/ssi.h"

#include "system/system.h"
#include "system/block-backend-io.h"
#include "qemu/error-report.h"
#include <stdint.h>
#include <zlib.h>

#include "hw/riscv/spacemit_k3.h"

#include "system/qtest.h"

#define SPACEMIT_K3_IRQCHIP_NUM_MSIS       511
#define SPACEMIT_K3_IRQCHIP_NUM_SOURCES    512
#define SPACEMIT_K3_IRQCHIP_NUM_PRIO_BITS  3
#define SPACEMIT_K3_IMSIC_GUESTS           5
#define SPACEMIT_K3_IMSIC_MSTRIDE          0x1000
#define SPACEMIT_K3_IMSIC_SSTRIDE          0x40000
#define SPACEMIT_K3_IMSIC_GUEST_BITS       6

#define SPACEMIT_K3_ACLINT_MTIME_SIZE    0xc000
#define SPACEMIT_K3_ACLINT_MTIME         0xbff8
#define SPACEMIT_K3_ACLINT_MTIMECMP      0x4000
#define SPACEMIT_K3_ACLINT_TIMEBASE_FREQ 24000000

#define SPACEMIT_K3_FSBL_NOR_OFFSET  0x0
#define SPACEMIT_K3_FSBL_SD_OFFSET   0x100000
#define SPACEMIT_K3_FSBL_EMMC_OFFSET 0x100000
#define SPACEMIT_K3_FSBL_UFS_OFFSET  0x100000

static const MemMapEntry spacemit_k3_memmap[] = {
    [SPACEMIT_K3_PCIE] =              {  0x80000000,    0x4000000 },
    [SPACEMIT_K3_ESPI_SLV_MEM] =      {  0x84000000,    0x2000000 },
    [SPACEMIT_K3_QSPI_AHB] =          {  0xb8000000,    0x8000000 },
    [SPACEMIT_K3_SRAM] =              {  0xc0800000,      0x80000 },
    [SPACEMIT_K3_AUDIO_PERIPH] =      {  0xc0880000,     0x180000 },
    [SPACEMIT_K3_DDR_TRAIN_BUF] =     {  0xc08d0000,      0x10000 },
    [SPACEMIT_K3_USB_OTG] =           {  0xc0a00000,     0x100000 },
    [SPACEMIT_K3_UFSHC] =             {  0xc0e00000,      0x40000 },
    [SPACEMIT_K3_ETH0] =              {  0xcac80000,       0x2000 },
    [SPACEMIT_K3_DP1] =               {  0xcac88000,       0x4000 },
    [SPACEMIT_K3_ESPI] =              {  0xcac8c000,       0x1000 },
    [SPACEMIT_K3_MAILBOX3] =          {  0xcac90c00,        0x400 },
    [SPACEMIT_K3_USB3] =              {  0xcad00000,     0x100000 },
    [SPACEMIT_K3_DDRC0] =             {  0xcb000000,    0x1000000 },
    [SPACEMIT_K3_DDRC1] =             {  0xcc000000,    0x1000000 },
    [SPACEMIT_K3_I2C1] =              {  0xd4011000,        0x800 },
    [SPACEMIT_K3_I2C2] =              {  0xd4012000,        0x800 },
    [SPACEMIT_K3_WDT0] =              {  0xd4014000,       0x1000 },
    [SPACEMIT_K3_I2C6] =              {  0xd4018800,        0x800 },
    [SPACEMIT_K3_I2C8] =              {  0xd401D800,        0x800 },
    [SPACEMIT_K3_SYSCON_APBC] =       {  0xd4015000,       0x1000 },
    [SPACEMIT_K3_THERMAL] =           {  0xd4018000,        0x100 },
    [SPACEMIT_K3_UART0] =             {  0xd4017000,        0x100 },
    [SPACEMIT_K3_GPIO] =              {  0xd4019000,        0x800 },
    [SPACEMIT_K3_PINCTRL] =           {  0xd401e000,       0x1000 },
    [SPACEMIT_K3_SYSCON_MPMU] =       {  0xd4050000,      0x10000 },
    [SPACEMIT_K3_PLL] =               {  0xd4090000,      0x10000 },
    [SPACEMIT_K3_QSPI] =              {  0xd420c000,       0x1000 },
    [SPACEMIT_K3_SDHCI0] =            {  0xd4280000,        0x200 },
    [SPACEMIT_K3_SDHCI1] =            {  0xd4280800,        0x200 },
    [SPACEMIT_K3_EMMC] =              {  0xd4281000,        0x200 },
    [SPACEMIT_K3_SYSCON_APMU] =       {  0xd4282800,        0x400 },
    [SPACEMIT_K3_CPU_CONFIG_UNIT] =   {  0xd4282C00,        0x400 },
    [SPACEMIT_K3_COUNTER] =           {  0xd5001000,         0x10 },
    [SPACEMIT_K3_SYSCON_DCIU] =       {  0xd8440000,       0xc000 },
    [SPACEMIT_K3_CCI550] =            {  0xd8500000,     0x100000 },
    [SPACEMIT_K3_SEC_APB_CLK_UNIT] =  {  0xf0610000,       0x2000 },
    [SPACEMIT_K3_SEC_JTAG_SOFTWARE] = {  0xf0618000,       0x1000 },
    [SPACEMIT_K3_SEC_BCM_CONFIG] =    {  0xf0700000,     0x100000 },
    [SPACEMIT_K3_MIMSIC] =            {  0xf1000000,      0x10000 },
    [SPACEMIT_K3_MAPLIC] =            {  0xf1800000,       0x4000 },
    [SPACEMIT_K3_ACLINT] =            {  0xf1810000,      0x10000 },
    [SPACEMIT_K3_BOOTROM] =           {  0xffe00000,     0x200000 },
    [SPACEMIT_K3_SIMSIC] =            {  0xe0400000,     0x200000 },
    [SPACEMIT_K3_SAPLIC] =            {  0xe0804000,       0x4000 },
    [SPACEMIT_K3_DRAM] =              { 0x100000000,  0x400000000 },
};

static int spacemit_k3_load_fsbl(SpacemitK3State *ms, DriveInfo *di,
                                 uint64_t bootinfo_offset,
                                 int selected_boot_mode)
{
    /* Read Bootinfo block */
    g_autofree void *bootinfo = g_malloc(0x44);
    uint32_t spl0_offset, spl_max_size, spl_size, target_crc, computed_crc;
    if (blk_pread(blk_by_legacy_dinfo(di), bootinfo_offset, 0x44, bootinfo, 0) <
        0) {
        error_report("Cannot read bootinfo from boot media");
        return -1;
    }

    /* Check CRC of Bootinfo block */
    target_crc = ldl_le_p(bootinfo + 0x40);
    computed_crc = crc32(0, bootinfo, 0x40);

    if (target_crc != computed_crc) {
        error_report("Bootinfo CRC doesn't match");
        return -1;
    }

    /* Get SPL Offset and Max Size from bootinfo */
    spl0_offset = ldl_le_p(bootinfo + 0x20);
    spl_max_size = ldl_le_p(bootinfo + 0x28);

    if (spl_max_size < 0x1000) {
        error_report("SPL Max Size value is too small");
        return -1;
    }

    /*
     * Every bootinfo block generated from the vendor
     * BSP seems to have this as the max value, so
     * cap it at that.
     */
    if (spl_max_size > 0x74000) {
        error_report("SPL Max Size value is too big");
        return -1;
    }

    g_autofree void *fsbl = g_malloc(spl_max_size);

    /* Read SPL */
    if (blk_pread(blk_by_legacy_dinfo(di), spl0_offset, spl_max_size, fsbl, 0) <
        0) {
        error_report("Cannot read FSBL from boot media");
        return -1;
    }

    spl_size = ldq_le_p(fsbl + 0xfe0 + 0x8);
    if (spl_size > spl_max_size - 0x1000) {
        error_report("SPL Size is too big");
        return -1;
    }

    /* Check CRC of SPL*/
    target_crc = ldl_le_p(fsbl + 0xfe0 + 28);
    computed_crc = crc32(0, fsbl + 0x1000, spl_size);

    if (target_crc != computed_crc) {
        error_report("FSBL CRC doesn't match");
        return -1;
    }

    rom_add_blob_fixed("spacemit-k3.fsbl", fsbl + 0x1000, spl_size,
                       ms->memmap[SPACEMIT_K3_SRAM].base + 0x1000);

    /*
     * Bootrom writes to this register to specify boot medium it picked
     * Subsequnent stages read this register. Simulate this write
     */
    spacemit_k3_set_selected_boot_mode(&ms->cpu_config, selected_boot_mode);
    return 0;
}

static int spacemit_k3_load_fsbl_sd(SpacemitK3State *ms)
{
    DriveInfo *sd = drive_get(IF_SD, 0, 0);
    if (!sd) {
        error_report("Failed to get sd");
        return -1;
    }

    return spacemit_k3_load_fsbl(ms, sd, SPACEMIT_K3_FSBL_SD_OFFSET,
                                 SPACEMIT_K3_SELECTED_BOOT_MODE_SD);
}

static int spacemit_k3_load_fsbl_emmc(SpacemitK3State *ms)
{
    DriveInfo *emmc = drive_get(IF_SD, 0, 2);
    if (!emmc) {
        error_report("Failed to get emmc");
        return -1;
    }

    return spacemit_k3_load_fsbl(ms, emmc, SPACEMIT_K3_FSBL_EMMC_OFFSET,
                                 SPACEMIT_K3_SELECTED_BOOT_MODE_EMMC);
}

static int spacemit_k3_load_fsbl_ufs(SpacemitK3State *ms)
{
    DriveInfo *ufs = drive_get(IF_SCSI, 0, 0);
    if (!ufs) {
        error_report("Failed to get ufs");
        return -1;
    }

    return spacemit_k3_load_fsbl(ms, ufs, SPACEMIT_K3_FSBL_UFS_OFFSET,
                                 SPACEMIT_K3_SELECTED_BOOT_MODE_UFS);
}

static int spacemit_k3_load_fsbl_nor(SpacemitK3State *ms)
{
    DriveInfo *nor = drive_get(IF_MTD, 0, 0);
    if (!nor) {
        error_report("Failed to get nor flash");
        return -1;
    }

    return spacemit_k3_load_fsbl(ms, nor, SPACEMIT_K3_FSBL_NOR_OFFSET,
                                 SPACEMIT_K3_SELECTED_BOOT_MODE_NOR);
}

static void spacemit_k3_machine_done(Notifier *notifier, void *data)
{
    SpacemitK3State *s = container_of(notifier, SpacemitK3State,
                                     machine_done);
    MachineState *machine = MACHINE(s);
    hwaddr start_addr = s->memmap[SPACEMIT_K3_BOOTROM].base;
    const char *firmware_name = riscv_default_firmware_name(&s->soc);
    RISCVBootInfo boot_info;

    riscv_boot_info_init(&boot_info, &s->soc);

    /*
     * Load bootrom supplied via -bios.
     * If no file is supplied, load next stage from supplied images
     * exactly as how bootrom would.
     */
    if (machine->firmware && strcmp(machine->firmware, "default") &&
        strcmp(machine->firmware, "none")) {
        riscv_find_and_load_firmware(machine, &boot_info, firmware_name,
                                     &start_addr, NULL);
    } else {
        switch (s->boot_mode) {
        case SPACEMIT_K3_BOOT_MODE_NOR:
            if (spacemit_k3_load_fsbl_sd(s) < 0) {
                error_report("Booting from SD failed, trying NOR");
                if (spacemit_k3_load_fsbl_nor(s) < 0) {
                    error_report("Booting from NOR Failed");
                    if (qtest_enabled()) {
                        return;
                    }
                    exit(1);
                }
            }
            break;
        case SPACEMIT_K3_BOOT_MODE_EMMC:
            if (spacemit_k3_load_fsbl_sd(s) < 0) {
                error_report("Booting from SD failed, trying EMMC");
                if (spacemit_k3_load_fsbl_emmc(s) < 0) {
                    error_report("Booting from EMMC Failed");
                    if (qtest_enabled()) {
                        return;
                    }
                    exit(1);
                }
            }
            break;
        case SPACEMIT_K3_BOOT_MODE_UFS:
            if (spacemit_k3_load_fsbl_sd(s) < 0) {
                error_report("Booting from SD failed, trying UFS");
                if (spacemit_k3_load_fsbl_ufs(s) < 0) {
                    error_report("Booting from UFS Failed");
                    if (qtest_enabled()) {
                        return;
                    }
                    exit(1);
                }
            }
            break;
        case SPACEMIT_K3_BOOT_MODE_NAND:
        case SPACEMIT_K3_BOOT_MODE_USB:
        case SPACEMIT_K3_BOOT_MODE_UART:
            error_report("Supply bootrom via -bios to use these boot methods");
            if (qtest_enabled()) {
                return;
            }
            exit(1);
        }
        s->soc.harts[0].env.resetvec =
            s->memmap[SPACEMIT_K3_SRAM].base + 0x1000;
    }
}

static void spacemit_k3_machine_init(MachineState *machine)
{
    SpacemitK3State *s = SPACEMIT_K3_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *bootrom = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *ddr_train_buf = g_new(MemoryRegion, 1);
    int hart_count = machine->smp.cpus;
    int i;

    s->memmap = spacemit_k3_memmap;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "hartid-base", 0,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "num-harts", hart_count,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "resetvec",
                            s->memmap[SPACEMIT_K3_BOOTROM].base,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    /*
     * Define APLIC and IMSIC nodes directly (without helper function)
     * so that we get handles to misic memory regions.
     */
    for (i = 0; i < hart_count; i++) {
        riscv_imsic_create(get_system_memory(),
                         s->memmap[SPACEMIT_K3_MIMSIC].base +
                             i * SPACEMIT_K3_IMSIC_MSTRIDE,
                         i, true, 1, SPACEMIT_K3_IRQCHIP_NUM_MSIS);
    }

    for (i = 0; i < hart_count; i++) {
        riscv_imsic_create(
          get_system_memory(),
          s->memmap[SPACEMIT_K3_SIMSIC].base + i * SPACEMIT_K3_IMSIC_SSTRIDE, i,
          false, 1 + SPACEMIT_K3_IMSIC_GUESTS, SPACEMIT_K3_IRQCHIP_NUM_MSIS);
    }

    s->irqchip = riscv_aplic_create(
        get_system_memory(), s->memmap[SPACEMIT_K3_MAPLIC].base,
        s->memmap[SPACEMIT_K3_MAPLIC].size, 0, 0,
        SPACEMIT_K3_IRQCHIP_NUM_SOURCES, SPACEMIT_K3_IRQCHIP_NUM_PRIO_BITS,
        true, true, NULL);

    riscv_aplic_create(get_system_memory(), s->memmap[SPACEMIT_K3_SAPLIC].base,
                       s->memmap[SPACEMIT_K3_SAPLIC].size, 0, 0,
                       SPACEMIT_K3_IRQCHIP_NUM_SOURCES,
                       SPACEMIT_K3_IRQCHIP_NUM_PRIO_BITS, true, false,
                       s->irqchip);

    riscv_aclint_mtimer_create(get_system_memory(),
            s->memmap[SPACEMIT_K3_ACLINT].base,
            SPACEMIT_K3_ACLINT_MTIME_SIZE,
            0, hart_count,
            SPACEMIT_K3_ACLINT_MTIMECMP,
            SPACEMIT_K3_ACLINT_MTIME,
            SPACEMIT_K3_ACLINT_TIMEBASE_FREQ, true);

    riscv_aclint_swi_create(get_system_memory(),
            s->memmap[SPACEMIT_K3_ACLINT].base,
            0, hart_count, false);

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, s->memmap[SPACEMIT_K3_DRAM].base,
                                machine->ram);

    /* boot rom */
    memory_region_init_rom(bootrom, NULL, "spacemit-k3.bootrom",
                           s->memmap[SPACEMIT_K3_BOOTROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                s->memmap[SPACEMIT_K3_BOOTROM].base, bootrom);

    memory_region_init_ram(sram, NULL, "sram",
                             s->memmap[SPACEMIT_K3_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                s->memmap[SPACEMIT_K3_SRAM].base, sram);

    memory_region_init_ram(ddr_train_buf, NULL, "ddr_train_buf",
                           s->memmap[SPACEMIT_K3_DDR_TRAIN_BUF].size,
                           &error_fatal);
    memory_region_add_subregion(system_memory,
                                s->memmap[SPACEMIT_K3_DDR_TRAIN_BUF].base,
                                ddr_train_buf);

    create_unimplemented_device("spacemit-k3.sec_jtag_software",
                                s->memmap[SPACEMIT_K3_SEC_JTAG_SOFTWARE].base,
                                s->memmap[SPACEMIT_K3_SEC_JTAG_SOFTWARE].size);

    create_unimplemented_device("spacemit-k3.syscon_apbc",
                                s->memmap[SPACEMIT_K3_SYSCON_APBC].base,
                                s->memmap[SPACEMIT_K3_SYSCON_APBC].size);

    create_unimplemented_device("spacemit-k3.sec_bcm_config",
                                s->memmap[SPACEMIT_K3_SEC_BCM_CONFIG].base,
                                s->memmap[SPACEMIT_K3_SEC_BCM_CONFIG].size);

    create_unimplemented_device("spacemit-k3.pll",
                                s->memmap[SPACEMIT_K3_PLL].base,
                                s->memmap[SPACEMIT_K3_PLL].size);

    create_unimplemented_device("spacemit-k3.pinctrl",
                                s->memmap[SPACEMIT_K3_PINCTRL].base,
                                s->memmap[SPACEMIT_K3_PINCTRL].size);

    create_unimplemented_device("spacemit-k3.counter",
                                s->memmap[SPACEMIT_K3_COUNTER].base,
                                s->memmap[SPACEMIT_K3_COUNTER].size);

    create_unimplemented_device("spacemit-k3.sec_apb_clk_unit",
                                s->memmap[SPACEMIT_K3_SEC_APB_CLK_UNIT].base,
                                s->memmap[SPACEMIT_K3_SEC_APB_CLK_UNIT].size);

    create_unimplemented_device("spacemit-k3.i2c1",
                                s->memmap[SPACEMIT_K3_I2C1].base,
                                s->memmap[SPACEMIT_K3_I2C1].size);

    create_unimplemented_device("spacemit-k3.i2c2",
                                s->memmap[SPACEMIT_K3_I2C2].base,
                                s->memmap[SPACEMIT_K3_I2C2].size);

    create_unimplemented_device("spacemit-k3.i2c6",
                                s->memmap[SPACEMIT_K3_I2C6].base,
                                s->memmap[SPACEMIT_K3_I2C6].size);

    create_unimplemented_device("spacemit-k3.i2c8",
                                s->memmap[SPACEMIT_K3_I2C8].base,
                                s->memmap[SPACEMIT_K3_I2C8].size);

    create_unimplemented_device("spacemit-k3.espi",
                                s->memmap[SPACEMIT_K3_ESPI].base,
                                s->memmap[SPACEMIT_K3_ESPI].size);

    create_unimplemented_device("spacemit-k3.audio_periph",
                                s->memmap[SPACEMIT_K3_AUDIO_PERIPH].base,
                                s->memmap[SPACEMIT_K3_AUDIO_PERIPH].size);

    create_unimplemented_device("spacemit-k3.mailbox3",
                                s->memmap[SPACEMIT_K3_MAILBOX3].base,
                                s->memmap[SPACEMIT_K3_MAILBOX3].size);

    create_unimplemented_device("spacemit-k3.syscon_dciu",
                                s->memmap[SPACEMIT_K3_SYSCON_DCIU].base,
                                s->memmap[SPACEMIT_K3_SYSCON_DCIU].size);

    create_unimplemented_device("spacemit-k3.cci550",
                                s->memmap[SPACEMIT_K3_CCI550].base,
                                s->memmap[SPACEMIT_K3_CCI550].size);

    create_unimplemented_device("spacemit-k3.espi_slv_mem",
                                s->memmap[SPACEMIT_K3_ESPI_SLV_MEM].base,
                                s->memmap[SPACEMIT_K3_ESPI_SLV_MEM].size);

    create_unimplemented_device("spacemit-k3.wdt0",
                                s->memmap[SPACEMIT_K3_WDT0].base,
                                s->memmap[SPACEMIT_K3_WDT0].size);

    create_unimplemented_device("spacemit-k3.dp1",
                                s->memmap[SPACEMIT_K3_DP1].base,
                                s->memmap[SPACEMIT_K3_DP1].size);

    create_unimplemented_device("spacemit-k3.gpio",
                                s->memmap[SPACEMIT_K3_GPIO].base,
                                s->memmap[SPACEMIT_K3_GPIO].size);

    create_unimplemented_device("spacemit-k3.pcie",
                                s->memmap[SPACEMIT_K3_PCIE].base,
                                s->memmap[SPACEMIT_K3_PCIE].size);

    create_unimplemented_device("spacemit-k3.usb_otg",
                                s->memmap[SPACEMIT_K3_USB_OTG].base,
                                s->memmap[SPACEMIT_K3_USB_OTG].size);

    create_unimplemented_device("spacemit-k3.eth0",
                                s->memmap[SPACEMIT_K3_ETH0].base,
                                s->memmap[SPACEMIT_K3_ETH0].size);

    create_unimplemented_device("spacemit-k3.usb3",
                                s->memmap[SPACEMIT_K3_USB3].base,
                                s->memmap[SPACEMIT_K3_USB3].size);

    create_unimplemented_device("spacemit-k3.thermal",
                                s->memmap[SPACEMIT_K3_THERMAL].base,
                                s->memmap[SPACEMIT_K3_THERMAL].size);

    object_initialize_child(OBJECT(machine), "spacemit-k3.cpu_config",
                            &s->cpu_config, TYPE_SPACEMIT_K3_CPU_CONFIG);
    object_property_set_int(OBJECT(&s->cpu_config),
                                "boot-mode", s->boot_mode, &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpu_config), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->cpu_config), 0,
                    s->memmap[SPACEMIT_K3_CPU_CONFIG_UNIT].base);

    object_initialize_child(OBJECT(machine), "spacemit-k3.ddrc0", &s->ddrc0,
                            TYPE_SPACEMIT_K3_DDRC);
    sysbus_realize(SYS_BUS_DEVICE(&s->ddrc0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddrc0), 0,
                    s->memmap[SPACEMIT_K3_DDRC0].base);

    object_initialize_child(OBJECT(machine), "spacemit-k3.ddrc1", &s->ddrc1,
                            TYPE_SPACEMIT_K3_DDRC);
    sysbus_realize(SYS_BUS_DEVICE(&s->ddrc1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddrc1), 0,
                    s->memmap[SPACEMIT_K3_DDRC1].base);

    object_initialize_child(OBJECT(machine), "spacemit-k3.syscon_apmu",
                            &s->syscon_apmu, TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT);
    sysbus_realize(SYS_BUS_DEVICE(&s->syscon_apmu), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->syscon_apmu), 0,
                    s->memmap[SPACEMIT_K3_SYSCON_APMU].base);

    object_initialize_child(OBJECT(machine), "spacemit-k3.syscon_mpmu",
                            &s->syscon_mpmu, TYPE_SPACEMIT_K3_SYSCON_MPMU_UNIT);
    sysbus_realize(SYS_BUS_DEVICE(&s->syscon_mpmu), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->syscon_mpmu), 0,
                    s->memmap[SPACEMIT_K3_SYSCON_MPMU].base);

    for (int num_sdhci = 0; num_sdhci < SPACEMIT_K3_NUM_SDHCI; num_sdhci++) {
        MemoryRegion *sdhci_vendor_region = g_new(MemoryRegion, 1);
        g_autofree char *sdhci_name =
            g_strdup_printf("spacemit-k3.sdhci%d", num_sdhci);
        g_autofree char *sdhci_vendor_name =
            g_strdup_printf("spacemit-k3.sdhci%d.vendor", num_sdhci);

        object_initialize_child(OBJECT(machine), sdhci_name,
                                &s->sdhci[num_sdhci], TYPE_SYSBUS_SDHCI);
        sysbus_realize(SYS_BUS_DEVICE(&s->sdhci[num_sdhci]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhci[num_sdhci]), 0,
                      s->memmap[SPACEMIT_K3_SDHCI0 + num_sdhci].base);
        sysbus_connect_irq(
          SYS_BUS_DEVICE(&s->sdhci[num_sdhci]), 0,
          qdev_get_gpio_in(s->irqchip, SPACEMIT_K3_SDHCI0_IRQ + num_sdhci));

        memory_region_init_ram(sdhci_vendor_region, NULL, sdhci_vendor_name,
                             0x100, &error_fatal);
        memory_region_add_subregion(
          system_memory, s->memmap[SPACEMIT_K3_SDHCI0 + num_sdhci].base + 0x100,
          sdhci_vendor_region);
    }

    MemoryRegion *emmc_vendor_region = g_new(MemoryRegion, 1);
    object_initialize_child(OBJECT(machine), "spacemit-k3.emmc", &s->emmc,
                            TYPE_SYSBUS_SDHCI);
    sysbus_realize(SYS_BUS_DEVICE(&s->emmc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->emmc), 0,
                    s->memmap[SPACEMIT_K3_EMMC].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->emmc), 0,
                       qdev_get_gpio_in(s->irqchip, SPACEMIT_K3_EMMC_IRQ));

    memory_region_init_ram(emmc_vendor_region, NULL, "spacemit-k3.emmc.vendor",
                           0x100, &error_fatal);
    memory_region_add_subregion(system_memory,
                                s->memmap[SPACEMIT_K3_EMMC].base + 0x100,
                                emmc_vendor_region);

    /*
     * Attach an SD/EMMC card image if one was provided via -drive if=sd
     * indices 0 and 1 are the 2 SD controllers, index 2 is EMMC
     */
    for (int sd_idx = 0; sd_idx < SPACEMIT_K3_NUM_SDHCI + 1; sd_idx++) {
        bool is_sd = sd_idx < 2;
        DriveInfo *dinfo = drive_get(IF_SD, 0, sd_idx);
        if (dinfo) {
            DeviceState *card =
              is_sd ? qdev_new(TYPE_SD_CARD) : qdev_new(TYPE_EMMC);
            qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                              &error_fatal);
            qdev_realize_and_unref(
                card,
                qdev_get_child_bus(DEVICE(is_sd ? &s->sdhci[sd_idx] : &s->emmc),
                                   "sd-bus"),
                &error_fatal);
        }
    }

    object_initialize_child(OBJECT(machine), "spacemit-k3.qspi", &s->qspi,
                            TYPE_SPACEMIT_K3_SPI);
    qdev_prop_set_uint32(DEVICE(&s->qspi), "ahb-base",
                         s->memmap[SPACEMIT_K3_QSPI_AHB].base);
    sysbus_realize(SYS_BUS_DEVICE(&s->qspi), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->qspi), 0,
                    s->memmap[SPACEMIT_K3_QSPI].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->qspi), 1,
                    s->memmap[SPACEMIT_K3_QSPI_AHB].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->qspi), 0,
                       qdev_get_gpio_in(s->irqchip, SPACEMIT_K3_QSPI_IRQ));

    /* Attach an NOR flash image if one was provided via -drive if=mtd */
    {
      DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
      if (dinfo) {
        DeviceState *flash = qdev_new("w25q64");
        qdev_prop_set_drive_err(flash, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(
            flash, qdev_get_child_bus(DEVICE(&s->qspi), "ssi"), &error_fatal);

        qdev_connect_gpio_out_named(
            DEVICE(&s->qspi), "cs", 0,
            qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0));
      }
    }

    object_initialize_child(OBJECT(machine), "spacemit-k3.ufs", &s->ufs,
                            TYPE_SPACEMIT_K3_UFS);
    sysbus_realize(SYS_BUS_DEVICE(&s->ufs), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ufs), 0,
                    s->memmap[SPACEMIT_K3_UFSHC].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ufs), 0,
                       qdev_get_gpio_in(s->irqchip, SPACEMIT_K3_UFSHC_IRQ));

    /* Attach a UFS image if one was provided via -drive if=scsi */
    {
      DriveInfo *dinfo = drive_get(IF_SCSI, 0, 0);
      if (dinfo) {
        DeviceState *lu = qdev_new(TYPE_UFS_LU);
        qdev_prop_set_drive_err(lu, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(lu, BUS(s->ufs.bus), &error_fatal);
      }
    }

    serial_mm_init(system_memory, s->memmap[SPACEMIT_K3_UART0].base, 2,
                   qdev_get_gpio_in(s->irqchip, SPACEMIT_K3_UART0_IRQ), 115200,
                   serial_hd(0), DEVICE_LITTLE_ENDIAN);

    s->machine_done.notify = spacemit_k3_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static char *spacemit_k3_get_boot_mode(Object *obj, Error **errp)
{
    SpacemitK3State *s = SPACEMIT_K3_MACHINE(obj);
    static const char * const boot_mode_names[] = {
        [SPACEMIT_K3_BOOT_MODE_EMMC] = "emmc",
        [SPACEMIT_K3_BOOT_MODE_NOR]  = "nor",
        [SPACEMIT_K3_BOOT_MODE_NAND] = "nand",
        [SPACEMIT_K3_BOOT_MODE_UFS]  = "ufs",
        [SPACEMIT_K3_BOOT_MODE_USB]  = "usb",
        [SPACEMIT_K3_BOOT_MODE_UART] = "uart",
    };

    return g_strdup(boot_mode_names[s->boot_mode]);
}

static void spacemit_k3_set_boot_mode(Object *obj, const char *value,
                                      Error **errp) {
  SpacemitK3State *s = SPACEMIT_K3_MACHINE(obj);
  int boot_mode = SPACEMIT_K3_BOOT_MODE_NOR; /* Default to NOR */

  if (!strcmp(value, "emmc")) {
    boot_mode = SPACEMIT_K3_BOOT_MODE_EMMC;
  } else if (!strcmp(value, "nor")) {
    boot_mode = SPACEMIT_K3_BOOT_MODE_NOR;
  } else if (!strcmp(value, "nand")) {
    boot_mode = SPACEMIT_K3_BOOT_MODE_NAND;
  } else if (!strcmp(value, "ufs")) {
    boot_mode = SPACEMIT_K3_BOOT_MODE_UFS;
  } else if (!strcmp(value, "usb")) {
    boot_mode = SPACEMIT_K3_BOOT_MODE_USB;
  } else if (!strcmp(value, "uart")) {
    boot_mode = SPACEMIT_K3_BOOT_MODE_UART;
  } else {
    error_setg(errp, "Invalid boot-mode '%s'", value);
  }
  s->boot_mode = boot_mode;
}

static void spacemit_k3_machine_instance_init(Object *obj)
{
    SpacemitK3State *s = SPACEMIT_K3_MACHINE(obj);

    s->boot_mode = SPACEMIT_K3_BOOT_MODE_NOR;
}

static void spacemit_k3_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Spacemit K3 board";
    mc->init = spacemit_k3_machine_init;
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    mc->default_ram_id = "spacemit-k3.dram";
    mc->default_ram_size = 16 * GiB;
    mc->default_cpu_type = TYPE_RISCV_CPU_SPACEMIT_X100;

    object_class_property_add_str(oc, "boot-mode", spacemit_k3_get_boot_mode,
                                  spacemit_k3_set_boot_mode);
    object_class_property_set_description(
        oc, "boot-mode",
        "Sets the boot select and download select/mode pins"
        "on the K3. Possible options are emmc,nor,nand,ufs"
        "usb,uart. The first 4 options default to SD first"
        "and the chosen option next.");
}

static const TypeInfo spacemit_k3_machine_typeinfo = {
    .name       = TYPE_SPACEMIT_K3_MACHINE,
    .parent     = TYPE_MACHINE,
    .instance_init = spacemit_k3_machine_instance_init,
    .class_init = spacemit_k3_machine_class_init,
    .instance_size = sizeof(SpacemitK3State),
};

static void spacemit_k3_machine_init_register_types(void)
{
    type_register_static(&spacemit_k3_machine_typeinfo);
}

type_init(spacemit_k3_machine_init_register_types)
