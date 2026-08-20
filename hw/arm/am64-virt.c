/*
 * AM64 virt machine model
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/char/pl011.h"
#include "hw/block/flash.h"
#include "hw/rtc/pl031.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci-host/gpex.h"
#include "hw/pci/pci.h"
#include "hw/arm/ti-am64x.h"
#include "hw/arm/k3-bootrom.h"
#include "hw/core/qdev-clock.h"
#include "hw/sd/sd.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "system/blockdev.h"
#include "qemu/error-report.h"
#include "chardev/char.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qapi/visitor.h"

#define AM64_VIRT_DRAM_BASE 0x80000000ULL
#define AM64_VIRT_UART0_BASE 0x09000000ULL
#define AM64_VIRT_UART1_BASE 0x09040000ULL
#define AM64_VIRT_UART0_IRQ 1
#define AM64_VIRT_UART1_IRQ 8
#define AM64_VIRT_RTC_BASE 0x09010000ULL
#define AM64_VIRT_RTC_IRQ 2
#define AM64_VIRT_GPIO_BASE 0x09030000ULL
#define AM64_VIRT_GPIO_IRQ 7
#define AM64_VIRT_FLASH_BASE 0x050000000ULL
#define AM64_VIRT_FLASH_SIZE 0x08000000ULL
#define AM64_VIRT_FLASH_SECTOR_SIZE (256 * KiB)
#define AM64_VIRT_PCIE_MMIO_BASE 0x68000000ULL
#define AM64_VIRT_PCIE_MMIO_SIZE 0x08000000ULL
#define AM64_VIRT_PCIE_PIO_BASE 0x3EFF0000ULL
#define AM64_VIRT_PCIE_PIO_SIZE 0x00010000ULL
#define AM64_VIRT_PCIE_ECAM_BASE 0x0D000000ULL
#define AM64_VIRT_PCIE_ECAM_SIZE 0x02000000ULL
#define AM64_VIRT_PCIE_IRQ_BASE 3

#define SYSCLK_FRQ 168000000ULL

typedef struct AM64VirtMachineState {
    MachineState parent_obj;
    TIAM64xState *soc;
    struct arm_boot_info bootinfo;
    int32_t m4boot_cpu;
} AM64VirtMachineState;

#define TYPE_AM64_VIRT_MACHINE MACHINE_TYPE_NAME("am64-virt")
OBJECT_DECLARE_SIMPLE_TYPE(AM64VirtMachineState, AM64_VIRT_MACHINE)

static void am64_virt_get_m4boot_cpu(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    AM64VirtMachineState *ams = AM64_VIRT_MACHINE(obj);
    int32_t value = ams->m4boot_cpu;

    visit_type_int32(v, name, &value, errp);
}

static void am64_virt_set_m4boot_cpu(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    AM64VirtMachineState *ams = AM64_VIRT_MACHINE(obj);
    int32_t value;

    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }
    if (value != -1 && value != 0) {
        error_setg(errp, "am64-virt: m4boot_cpu must be -1 or 0");
        return;
    }
    ams->m4boot_cpu = value;
}

static void am64_virt_machine_instance_init(Object *obj)
{
    AM64VirtMachineState *ams = AM64_VIRT_MACHINE(obj);

    ams->m4boot_cpu = -1;
    object_property_add_alias(obj, "m4boot_cpu", obj, "m4boot-cpu");
}

static void am64_virt_create_uart(hwaddr base, int irq, Chardev *chr,
                                  DeviceState *gic)
{
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    MemoryRegion *sysmem = get_system_memory();

    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(gic, irq));
}

static void am64_virt_create_pcie(const AM64VirtMachineState *ams,
                                  DeviceState *gic)
{
    DeviceState *dev = qdev_new(TYPE_GPEX_HOST);
    PCIHostState *pci;
    MemoryRegion *mmio_alias;
    MemoryRegion *ecam_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_reg;
    int i;

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, AM64_VIRT_PCIE_ECAM_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                AM64_VIRT_PCIE_ECAM_BASE, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, AM64_VIRT_PCIE_MMIO_BASE,
                             AM64_VIRT_PCIE_MMIO_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                AM64_VIRT_PCIE_MMIO_BASE, mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, AM64_VIRT_PCIE_PIO_BASE);

    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(gic,
                                            AM64_VIRT_PCIE_IRQ_BASE + i));
        gpex_set_irq_num(GPEX_HOST(dev), i, AM64_VIRT_PCIE_IRQ_BASE + i);
    }

    pci = PCI_HOST_BRIDGE(dev);
    pci_init_nic_devices(pci->bus, MACHINE_GET_CLASS(ams)->default_nic);
}

static void am64_virt_create_rtc(hwaddr base, int irq, DeviceState *gic)
{
    sysbus_create_simple(TYPE_PL031, base, qdev_get_gpio_in(gic, irq));
}

static void am64_virt_create_gpio(hwaddr base, int irq, DeviceState *gic)
{
    sysbus_create_simple("pl061", base, qdev_get_gpio_in(gic, irq));
}

static void am64_virt_create_flash(void)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint32(dev, "num-blocks",
                         AM64_VIRT_FLASH_SIZE / AM64_VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint64(dev, "sector-length", AM64_VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", "am64-virt.flash0");

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, AM64_VIRT_FLASH_BASE);
}

static void am64_virt_init(MachineState *machine)
{
    AM64VirtMachineState *ams = AM64_VIRT_MACHINE(machine);
    DeviceState *soc = qdev_new(TYPE_TI_AM64X);
    DeviceState *gic;
    Clock *sysclk = clock_new(OBJECT(machine), "SYSCLK");
    Chardev *mcu_chardev;
    uint8_t a53_cpus = MIN(machine->smp.cpus, TI_AM64X_A53_NUM);
    DriveInfo *sd_di = drive_get(IF_SD, 0, 0);

    /*
     * The SoC has M4 and R5F vCPUs beyond the A53s. Since TCG allocates
     * context slots from smp.max_cpus at accelerator init, fail before realize.
     */
    if (machine->smp.max_cpus < a53_cpus + 2) {
        error_report("am64-virt: -smp maxcpus=%d is too small for %d A53 "
                     "+ M4 + R5F vCPUs; raise maxcpus (e.g. "
                     "-smp cpus=2,maxcpus=7) or omit -smp",
                     machine->smp.max_cpus, a53_cpus);
        exit(1);
    }

    clock_set_hz(sysclk, SYSCLK_FRQ);
    qdev_prop_set_uint8(soc, "a53-cpus", a53_cpus);
    qdev_prop_set_uint64(soc, "ram-base", AM64_VIRT_DRAM_BASE);
    qdev_prop_set_uint64(soc, "ram-size", machine->ram_size);
    qdev_connect_clock_in(soc, "sysclk", sysclk);
    mcu_chardev = qemu_chr_find("uart0");
    if (mcu_chardev) {
        qdev_prop_set_chr(DEVICE(&TI_AM64X(soc)->mcu_uart[0]),
                          "chardev", mcu_chardev);
    }
    if (ams->m4boot_cpu >= 0) {
        if (ams->m4boot_cpu != 0) {
            error_report("am64-virt: m4boot_cpu only supports value 0");
            exit(1);
        }
        qdev_prop_set_bit(soc, "a53-start-powered-off", true);
        qdev_prop_set_bit(soc, "m4-start-powered-off", false);
    }

    if (machine->firmware && ams->m4boot_cpu >= 0) {
        error_report("am64-virt: -bios and m4boot-cpu are mutually "
                     "exclusive");
        exit(1);
    }
    if (machine->firmware && machine->kernel_filename) {
        /*
         * ROM boot consumes -bios via k3_bootrom_load() and never calls
         * arm_load_kernel(), therefore reject a silently ignored -kernel.
         */
        error_report("am64-virt: -bios and -kernel are mutually exclusive");
        exit(1);
    }
    if (machine->firmware) {
        /* ROM boot starts only R5F boot core. */
        qdev_prop_set_bit(soc, "a53-start-powered-off", true);
        qdev_prop_set_bit(soc, "m4-start-powered-off", true);
        qdev_prop_set_bit(soc, "r5-start-powered-off", false);
        qdev_prop_set_chr(DEVICE(&TI_AM64X(soc)->main_uart0),
                          "chardev", serial_hd(0));
        if (sd_di) {
            /*
             * DEVSTAT primary bootmode MMC (0x8), SD port: describe MMC2
             * (sdhci[1]) FS-mode boot, not MMC1 raw mode.
             */
            qdev_prop_set_uint32(DEVICE(&TI_AM64X(soc)->ctrlmmr), "devstat",
                                 0x240);
        }
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(soc), &error_fatal);
    ams->soc = TI_AM64X(soc);
    gic = DEVICE(&ams->soc->gic);

    if (sd_di) {
        DeviceState *card = qdev_new(TYPE_SD_CARD);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(sd_di),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(&ams->soc->sdhci[1]),
                                                  "sd-bus"),
                               &error_fatal);
    }

    memory_region_add_subregion(get_system_memory(), AM64_VIRT_DRAM_BASE,
                                machine->ram);

    am64_virt_create_uart(AM64_VIRT_UART0_BASE, AM64_VIRT_UART0_IRQ,
                          machine->firmware ? serial_hd(1) : serial_hd(0),
                          gic);
    am64_virt_create_uart(AM64_VIRT_UART1_BASE, AM64_VIRT_UART1_IRQ,
                          machine->firmware ? serial_hd(2) : serial_hd(1),
                          gic);
    am64_virt_create_rtc(AM64_VIRT_RTC_BASE, AM64_VIRT_RTC_IRQ, gic);
    am64_virt_create_gpio(AM64_VIRT_GPIO_BASE, AM64_VIRT_GPIO_IRQ, gic);
    am64_virt_create_flash();
    am64_virt_create_pcie(ams, gic);

    memset(&ams->bootinfo, 0, sizeof(ams->bootinfo));
    ams->bootinfo.ram_size = machine->ram_size;
    ams->bootinfo.loader_start = AM64_VIRT_DRAM_BASE;
    ams->bootinfo.board_id = -1;
    ams->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    g_assert(qemu_get_cpu(0));
    if (machine->firmware) {
        g_autofree char *fn =
            qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);

        k3_bootrom_load(ams->soc, fn ? fn : machine->firmware,
                        &error_fatal);
    } else if (ams->m4boot_cpu < 0) {
        arm_load_kernel(ARM_CPU(qemu_get_cpu(0)), machine, &ams->bootinfo);
    }
}

static void am64_virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "AM64 virt machine";
    mc->init = am64_virt_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->default_nic = "virtio-net-pci";
    mc->default_ram_id = "am64-virt.ram";
    /*
     * Real AM64x has four R5F cores plus one M4 beyond the A53 cluster.
     * Reserve max_cpus headroom for the not yet modeled R5F cores.
     */
    mc->max_cpus = TI_AM64X_A53_NUM + 4 + 1; /* 4 R5F cores, 1 M4 */
    /*
     * Default to the full heterogeneous vCPU budget, so plain invocations
     * have enough TCG slots for A53, R5F and M4 cores.
     */
    mc->default_cpus = mc->max_cpus;
    mc->default_ram_size = 2 * GiB;

    object_class_property_add(oc, "m4boot-cpu", "int32",
                              am64_virt_get_m4boot_cpu,
                              am64_virt_set_m4boot_cpu,
                              NULL, NULL);
    object_class_property_set_description(oc, "m4boot-cpu",
                                          "Set to 0 to boot only the M4 core");
}

static const TypeInfo am64_virt_machine_info = {
    .name = TYPE_AM64_VIRT_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(AM64VirtMachineState),
    .instance_init = am64_virt_machine_instance_init,
    .class_init = am64_virt_machine_class_init,
    .interfaces = arm_aarch64_machine_interfaces,
};

static void am64_virt_machine_init_register_types(void)
{
    type_register_static(&am64_virt_machine_info);
}

type_init(am64_virt_machine_init_register_types)
