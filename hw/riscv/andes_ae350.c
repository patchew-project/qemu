/*
 * Andes RISC-V AE350 Board
 *
 * Copyright (c) 2021 Andes Tech. Corp.
 *
 * Andes AE350 Board supports ns16550a UART and VirtIO MMIO.
 * The interrupt controllers are andes PLIC and andes PLICSW.
 * Timer is Andes PLMT.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"

#include "hw/intc/andes_plic.h"
#include "hw/timer/andes_plmt.h"
#include "hw/riscv/andes_ae350.h"

# define BIOS_FILENAME ""

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} andes_ae350_memmap[] = {
    [ANDES_AE350_DEBUG]     = { 0x00000000,      0x100 },
    [ANDES_AE350_DRAM]      = { 0x00000000, 0x80000000 },
    [ANDES_AE350_MROM]      = { 0xb0000000,   0x100000 },
    [ANDES_AE350_MAC]       = { 0xe0100000,   0x100000 },
    [ANDES_AE350_GEM]       = { 0xe0200000,   0x100000 },
    [ANDES_AE350_PLIC]      = { 0xe4000000,   0x400000 },
    [ANDES_AE350_PLMT]      = { 0xe6000000,   0x100000 },
    [ANDES_AE350_PLICSW]    = { 0xe6400000,   0x400000 },
    [ANDES_AE350_UART1]     = { 0xf0200000,      0x100 },
    [ANDES_AE350_UART2]     = { 0xf0300000,      0x100 },
    [ANDES_AE350_PIT]       = { 0xf0400000,   0x100000 },
    [ANDES_AE350_SDC]       = { 0xf0e00000,   0x100000 },
    [ANDES_AE350_VIRTIO]    = { 0xfe000000,     0x1000 },
};

static void
create_fdt(AndesAe350BoardState *bs, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    AndesAe350SocState *s = &bs->soc;
    MachineState *ms = MACHINE(qdev_get_machine());
    void *fdt;
    int cpu, i;
    uint64_t mem_addr;
    uint32_t *plic_irq_ext, *plicsw_irq_ext, *plmt_irq_ext;
    unsigned long plic_addr, plicsw_addr, plmt_addr;
    char *plic_name, *plicsw_name, *plmt_name;
    uint32_t intc_phandle = 0, plic_phandle = 0;
    uint32_t phandle = 1;
    char *isa_name, *mem_name, *cpu_name, *intc_name, *uart_name, *virtio_name;

    if (ms->dtb) {
        fdt = bs->fdt = load_device_tree(ms->dtb, &bs->fdt_size);
        if (!fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
        goto update_bootargs;
    } else {
        fdt = bs->fdt = create_device_tree(&bs->fdt_size);
        if (!fdt) {
            error_report("create_device_tree() failed");
            exit(1);
        }
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "Andes AE350 Board");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "andestech,ae350");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          ANDES_PLMT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map");

    plic_irq_ext = g_new0(uint32_t, s->cpus.num_harts * 4);
    plicsw_irq_ext = g_new0(uint32_t, s->cpus.num_harts * 2);
    plmt_irq_ext = g_new0(uint32_t, s->cpus.num_harts * 2);

    for (cpu = 0; cpu < s->cpus.num_harts; cpu++) {
        intc_phandle = phandle++;

        cpu_name = g_strdup_printf("/cpus/cpu@%d",
            s->cpus.hartid_base + cpu);
        qemu_fdt_add_subnode(fdt, cpu_name);
#if defined(TARGET_RISCV32)
        qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv32");
#else
        qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv39");
#endif
        isa_name = riscv_isa_string(&s->cpus.harts[cpu]);
        qemu_fdt_setprop_string(fdt, cpu_name, "riscv,isa", isa_name);
        g_free(isa_name);
        qemu_fdt_setprop_string(fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(fdt, cpu_name, "reg",
            s->cpus.hartid_base + cpu);
        qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(fdt, intc_name);
        qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_phandle);
        qemu_fdt_setprop_string(fdt, intc_name, "compatible",
            "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);

        plic_irq_ext[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        plic_irq_ext[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        plic_irq_ext[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        plic_irq_ext[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);

        plicsw_irq_ext[cpu * 2 + 0] = cpu_to_be32(intc_phandle);
        plicsw_irq_ext[cpu * 2 + 1] = cpu_to_be32(IRQ_M_SOFT);

        plmt_irq_ext[cpu * 2 + 0] = cpu_to_be32(intc_phandle);
        plmt_irq_ext[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);

        g_free(intc_name);
    }

    mem_addr = memmap[ANDES_AE350_DRAM].base;
    mem_name = g_strdup_printf("/memory@%lx", (long)mem_addr);
    qemu_fdt_add_subnode(fdt, mem_name);
    qemu_fdt_setprop_cells(fdt, mem_name, "reg",
        mem_addr >> 32, mem_addr, mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, mem_name, "device_type", "memory");
    g_free(mem_name);

    /* create plic */
    plic_phandle = phandle++;
    plic_addr = memmap[ANDES_AE350_PLIC].base;
    plic_name = g_strdup_printf("/soc/interrupt-controller@%lx", plic_addr);
    qemu_fdt_add_subnode(fdt, plic_name);
    qemu_fdt_setprop_cell(fdt, plic_name,
        "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, plic_name,
        "#interrupt-cells", 0x2);
    qemu_fdt_setprop_string(fdt, plic_name, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, plic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, plic_name, "interrupts-extended",
        plic_irq_ext, s->cpus.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, plic_name, "reg",
        0x0, plic_addr, 0x0, memmap[ANDES_AE350_PLIC].size);
    qemu_fdt_setprop_cell(fdt, plic_name, "riscv,ndev", 0x47);
    qemu_fdt_setprop_cell(fdt, plic_name, "phandle", plic_phandle);
    g_free(plic_name);
    g_free(plic_irq_ext);

    /* create plicsw */
    plicsw_addr = memmap[ANDES_AE350_PLICSW].base;
    plicsw_name = g_strdup_printf("/soc/interrupt-controller@%lx", plicsw_addr);
    qemu_fdt_add_subnode(fdt, plicsw_name);
    qemu_fdt_setprop_cell(fdt, plicsw_name,
        "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, plicsw_name,
        "#interrupt-cells", 0x2);
    qemu_fdt_setprop_string(fdt, plicsw_name, "compatible", "riscv,plic1");
    qemu_fdt_setprop(fdt, plicsw_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, plicsw_name, "interrupts-extended",
        plicsw_irq_ext, s->cpus.num_harts * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cells(fdt, plicsw_name, "reg",
        0x0, plicsw_addr, 0x0, memmap[ANDES_AE350_PLIC].size);
    qemu_fdt_setprop_cell(fdt, plicsw_name, "riscv,ndev", 0x1);
    g_free(plicsw_name);
    g_free(plicsw_irq_ext);

    /* create plmt */
    plmt_addr = memmap[ANDES_AE350_PLMT].base;
    plmt_name = g_strdup_printf("/soc/plmt0@%lx", plmt_addr);
    qemu_fdt_add_subnode(fdt, plmt_name);
    qemu_fdt_setprop_string(fdt, plmt_name, "compatible", "riscv,plmt0");
    qemu_fdt_setprop(fdt, plmt_name, "interrupts-extended",
        plmt_irq_ext, s->cpus.num_harts * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cells(fdt, plmt_name, "reg",
        0x0, plmt_addr, 0x0, memmap[ANDES_AE350_PLMT].size);
    g_free(plmt_name);
    g_free(plmt_irq_ext);

    uart_name = g_strdup_printf("/serial@%lx", memmap[ANDES_AE350_UART1].base);
    qemu_fdt_add_subnode(fdt, uart_name);
    qemu_fdt_setprop_string(fdt, uart_name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, uart_name, "reg",
        0x0, memmap[ANDES_AE350_UART1].base,
        0x0, memmap[ANDES_AE350_UART1].size);
    qemu_fdt_setprop_cell(fdt, uart_name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, uart_name, "reg-shift", ANDES_UART_REG_SHIFT);
    qemu_fdt_setprop_cell(fdt, uart_name, "reg-offset", ANDES_UART_REG_OFFSET);
    qemu_fdt_setprop_cell(fdt, uart_name, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, uart_name, "interrupts", ANDES_AE350_UART1_IRQ, 0x4);

    uart_name = g_strdup_printf("/serial@%lx", memmap[ANDES_AE350_UART2].base);
    qemu_fdt_add_subnode(fdt, uart_name);
    qemu_fdt_setprop_string(fdt, uart_name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, uart_name, "reg",
        0x0, memmap[ANDES_AE350_UART2].base,
        0x0, memmap[ANDES_AE350_UART2].size);
    qemu_fdt_setprop_cell(fdt, uart_name, "reg-shift", ANDES_UART_REG_SHIFT);
    qemu_fdt_setprop_cell(fdt, uart_name, "reg-offset", ANDES_UART_REG_OFFSET);
    qemu_fdt_setprop_cell(fdt, uart_name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, uart_name, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, uart_name, "interrupts", ANDES_AE350_UART2_IRQ, 0x4);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
            "console=ttyS0,38400n8 earlycon=sbi debug loglevel=7");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", uart_name);
    g_free(uart_name);

    for (i = 0; i < ANDES_AE350_VIRTIO_COUNT; i++) {
        virtio_name = g_strdup_printf("/virtio_mmio@%lx",
            (memmap[ANDES_AE350_VIRTIO].base + i * memmap[ANDES_AE350_VIRTIO].size));
        qemu_fdt_add_subnode(fdt, virtio_name);
        qemu_fdt_setprop_string(fdt, virtio_name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(fdt, virtio_name, "reg",
            0x0, memmap[ANDES_AE350_VIRTIO].base + i * memmap[ANDES_AE350_VIRTIO].size,
            0x0, memmap[ANDES_AE350_VIRTIO].size);
        qemu_fdt_setprop_cell(fdt, virtio_name, "interrupt-parent",
                                plic_phandle);
        qemu_fdt_setprop_cells(fdt, virtio_name, "interrupts",
                                ANDES_AE350_VIRTIO_IRQ + i, 0x4);
        g_free(virtio_name);
    }

update_bootargs:
    if (cmdline && cmdline[0] != '\0') {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
}

static char *init_hart_config(const char *hart_config, int num_harts)
{
    int length = 0, i = 0;
    char *result;

    length = (strlen(hart_config) + 1) * num_harts;
    result = g_malloc0(length);
    for (i = 0; i < num_harts; i++) {
        if (i != 0) {
            strncat(result, ",", length);
        }
        strncat(result, hart_config, length);
        length -= (strlen(hart_config) + 1);
    }

    return result;
}

static void andes_ae350_soc_realize(DeviceState *dev_soc, Error **errp)
{
    const struct MemmapEntry *memmap = andes_ae350_memmap;
    MachineState *machine = MACHINE(qdev_get_machine());
    MemoryRegion *system_memory = get_system_memory();
    AndesAe350SocState *s = ANDES_AE350_SOC(dev_soc);
    char *plic_hart_config, *plicsw_hart_config;

    plicsw_hart_config =
        init_hart_config(ANDES_AE350_PLICSW_HART_CONFIG, machine->smp.cpus);

    /* Per-socket SW-PLIC */
    s->plic_sw = andes_plicsw_create(
        memmap[ANDES_AE350_PLICSW].base,
        ANDES_AE350_PLICSW_NAME,
        plicsw_hart_config,
        ANDES_AE350_PLICSW_NUM_SOURCES,
        ANDES_AE350_PLICSW_NUM_PRIORITIES,
        ANDES_AE350_PLICSW_PRIORITY_BASE,
        ANDES_AE350_PLICSW_PENDING_BASE,
        ANDES_AE350_PLICSW_ENABLE_BASE,
        ANDES_AE350_PLICSW_ENABLE_STRIDE,
        ANDES_AE350_PLICSW_THRESHOLD_BASE,
        ANDES_AE350_PLICSW_THRESHOLD_STRIDE,
        memmap[ANDES_AE350_PLICSW].size);

    g_free(plicsw_hart_config);

    andes_plmt_create(memmap[ANDES_AE350_PLMT].base,
                memmap[ANDES_AE350_PLMT].size,
                machine->smp.cpus, ANDES_PLMT_TIME_BASE, ANDES_PLMT_TIMECMP_BASE);

    plic_hart_config =
        init_hart_config(ANDES_AE350_PLIC_HART_CONFIG, machine->smp.cpus);

    /* Per-socket PLIC */
    s->plic = andes_plic_create(
        memmap[ANDES_AE350_PLIC].base,
        ANDES_AE350_PLIC_NAME,
        plic_hart_config,
        ANDES_AE350_PLIC_NUM_SOURCES,
        ANDES_AE350_PLIC_NUM_PRIORITIES,
        ANDES_AE350_PLIC_PRIORITY_BASE,
        ANDES_AE350_PLIC_PENDING_BASE,
        ANDES_AE350_PLIC_ENABLE_BASE,
        ANDES_AE350_PLIC_ENABLE_STRIDE,
        ANDES_AE350_PLIC_THRESHOLD_BASE,
        ANDES_AE350_PLIC_THRESHOLD_STRIDE,
        memmap[ANDES_AE350_PLIC].size);

    g_free(plic_hart_config);

    /* VIRTIO */
    for (int i = 0; i < ANDES_AE350_VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[ANDES_AE350_VIRTIO].base + i * memmap[ANDES_AE350_VIRTIO].size,
            qdev_get_gpio_in(DEVICE(s->plic), (ANDES_AE350_VIRTIO_IRQ + i)));
    }

    serial_mm_init(system_memory,
        memmap[ANDES_AE350_UART1].base + ANDES_UART_REG_OFFSET,
        ANDES_UART_REG_SHIFT,
        qdev_get_gpio_in(DEVICE(s->plic), ANDES_AE350_UART1_IRQ),
        38400, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    serial_mm_init(system_memory,
        memmap[ANDES_AE350_UART2].base + ANDES_UART_REG_OFFSET,
        ANDES_UART_REG_SHIFT,
        qdev_get_gpio_in(DEVICE(s->plic), ANDES_AE350_UART2_IRQ),
        38400, serial_hd(0), DEVICE_LITTLE_ENDIAN);
}

static void andes_ae350_soc_instance_init(Object *obj)
{
    const struct MemmapEntry *memmap = andes_ae350_memmap;
    MachineState *machine = MACHINE(qdev_get_machine());
    AndesAe350SocState *s = ANDES_AE350_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->cpus), "cpu-type",
                            machine->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts",
                            machine->smp.cpus, &error_abort);
    qdev_prop_set_uint64(DEVICE(&s->cpus), "resetvec",
                            memmap[ANDES_AE350_MROM].base);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);
}

static void andes_ae350_machine_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = andes_ae350_memmap;

    AndesAe350BoardState *bs = ANDES_AE350_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    target_ulong start_addr = memmap[ANDES_AE350_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    uint32_t fdt_load_addr;
    uint64_t kernel_entry;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc",
                    &bs->soc, TYPE_ANDES_AE350_SOC);
    qdev_realize(DEVICE(&bs->soc), NULL, &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.andes.ae350.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[ANDES_AE350_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(bs, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.andes.ae350.mrom",
                           memmap[ANDES_AE350_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[ANDES_AE350_MROM].base,
                                mask_rom);

    firmware_end_addr = riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                                     start_addr, NULL);
    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&bs->soc.cpus,
                                                         firmware_end_addr);

        kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                         kernel_start_addr, NULL);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(bs->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(bs->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    } else {
       /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;
    }

    /* Compute the fdt load address in dram */
    fdt_load_addr = riscv_load_fdt(memmap[ANDES_AE350_DRAM].base,
                                   machine->ram_size, bs->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &bs->soc.cpus, start_addr,
                andes_ae350_memmap[ANDES_AE350_MROM].base,
                andes_ae350_memmap[ANDES_AE350_MROM].size, kernel_entry,
                fdt_load_addr, bs->fdt);
}

static void andes_ae350_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Board compatible with Andes AE350";
    mc->init = andes_ae350_machine_init;
    mc->max_cpus = ANDES_CPUS_MAX;
    mc->default_cpu_type = VIRT_CPU;
}

static void andes_ae350_machine_instance_init(Object *obj)
{

}

static const TypeInfo andes_ae350_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("andes_ae350"),
    .parent     = TYPE_MACHINE,
    .class_init = andes_ae350_machine_class_init,
    .instance_init = andes_ae350_machine_instance_init,
    .instance_size = sizeof(AndesAe350BoardState),
};

static void andes_ae350_machine_init_register_types(void)
{
    type_register_static(&andes_ae350_machine_typeinfo);
}

type_init(andes_ae350_machine_init_register_types)

static void andes_ae350_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = andes_ae350_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo andes_ae350_soc_type_info = {
    .name       = TYPE_ANDES_AE350_SOC,
    .parent     = TYPE_DEVICE,
    .instance_init = andes_ae350_soc_instance_init,
    .instance_size = sizeof(AndesAe350SocState),
    .class_init = andes_ae350_soc_class_init,
};

static void andes_ae350_soc_init_register_types(void)
{
    type_register_static(&andes_ae350_soc_type_info);
}

type_init(andes_ae350_soc_init_register_types)
