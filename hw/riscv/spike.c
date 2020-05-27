/*
 * QEMU RISC-V Spike Board
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This provides a RISC-V Board with the following devices:
 *
 * 0) HTIF Console and Poweroff
 * 1) CLINT (Timer and IPI)
 * 2) PLIC (Platform Level Interrupt Controller)
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_htif.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/spike.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/qtest.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include <libfdt.h>

#if defined(TARGET_RISCV32)
# define BIOS_FILENAME "opensbi-riscv32-spike-fw_jump.elf"
#else
# define BIOS_FILENAME "opensbi-riscv64-spike-fw_jump.elf"
#endif

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} spike_memmap[] = {
    [SPIKE_MROM] =     {     0x1000,    0x11000 },
    [SPIKE_CLINT] =    {  0x2000000,    0x10000 },
    [SPIKE_DRAM] =     { 0x80000000,        0x0 },
};

static void create_fdt(SpikeState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu, socket;
    uint32_t *clint_cells;
    unsigned long clint_addr;
    uint32_t cpu_phandle, intc_phandle, phandle = 1;
    char *name, *clint_name, *clust_name, *core_name, *cpu_name, *intc_name;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "ucbbar,spike-bare,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "ucbbar,spike-bare-dev");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/htif");
    qemu_fdt_setprop_string(fdt, "/htif", "compatible", "ucb,htif0");

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    name = g_strdup_printf("/memory@%lx", (long)memmap[SPIKE_DRAM].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_cells(fdt, name, "reg",
        memmap[SPIKE_DRAM].base >> 32, memmap[SPIKE_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
    g_free(name);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
        SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map");

    for (socket = (s->num_socs - 1); socket >= 0; socket--) {
        clust_name = g_strdup_printf("/cpus/cpu-map/cluster%d", socket);
        qemu_fdt_add_subnode(fdt, clust_name);

        clint_cells =  g_new0(uint32_t, s->soc[socket].num_harts * 4);

        for (cpu = s->soc[socket].num_harts - 1; cpu >= 0; cpu--) {
            cpu_phandle = phandle++;

            cpu_name = g_strdup_printf("/cpus/cpu@%d",
                s->soc[socket].hartid_base + cpu);
            qemu_fdt_add_subnode(fdt, cpu_name);
#if defined(TARGET_RISCV32)
            qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv32");
#else
            qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv48");
#endif
            name = riscv_isa_string(&s->soc[socket].harts[cpu]);
            qemu_fdt_setprop_string(fdt, cpu_name, "riscv,isa", name);
            g_free(name);
            qemu_fdt_setprop_string(fdt, cpu_name, "compatible", "riscv");
            qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
            qemu_fdt_setprop_cell(fdt, cpu_name, "reg",
                s->soc[socket].hartid_base + cpu);
            qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
            qemu_fdt_setprop_cell(fdt, cpu_name, "numa-node-id", socket);
            qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);

            intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
            qemu_fdt_add_subnode(fdt, intc_name);
            intc_phandle = phandle++;
            qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_phandle);
            qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                "riscv,cpu-intc");
            qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);

            clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
            clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
            clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
            clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);

            core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
            qemu_fdt_add_subnode(fdt, core_name);
            qemu_fdt_setprop_cell(fdt, core_name, "cpu", cpu_phandle);

            g_free(core_name);
            g_free(intc_name);
            g_free(cpu_name);
        }

        clint_addr = memmap[SPIKE_CLINT].base +
            (memmap[SPIKE_CLINT].size * socket);
        clint_name = g_strdup_printf("/soc/clint@%lx", clint_addr);
        qemu_fdt_add_subnode(fdt, clint_name);
        qemu_fdt_setprop_string(fdt, clint_name, "compatible", "riscv,clint0");
        qemu_fdt_setprop_cells(fdt, clint_name, "reg",
            0x0, clint_addr, 0x0, memmap[SPIKE_CLINT].size);
        qemu_fdt_setprop(fdt, clint_name, "interrupts-extended",
            clint_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
        qemu_fdt_setprop_cell(fdt, clint_name, "numa-node-id", socket);

        g_free(clint_name);
        g_free(clint_cells);
        g_free(clust_name);
    }

    if (cmdline) {
        qemu_fdt_add_subnode(fdt, "/chosen");
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
}

static void spike_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = spike_memmap;
    SpikeState *s = SPIKE_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    int i;
    char *soc_name;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int base_hartid, cpus_per_socket;

    /* Figure-out number of sockets */
    s->num_socs = (s->multi_socket) ? machine->smp.sockets : 1;

    /* Limit the number of sockets */
    if (SPIKE_SOCKETS_MAX < s->num_socs) {
        s->num_socs = SPIKE_SOCKETS_MAX;
    }

    /* Initialize socket */
    for (i = 0; i < s->num_socs; i++) {
        base_hartid = i * (smp_cpus / s->num_socs);
        if (i == (s->num_socs - 1)) {
            cpus_per_socket = smp_cpus - base_hartid;
        } else {
            cpus_per_socket = smp_cpus / s->num_socs;
        }
        soc_name = g_strdup_printf("soc%d", i);
        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
            sizeof(s->soc[i]), TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
        g_free(soc_name);
        object_property_set_str(OBJECT(&s->soc[i]),
            machine->cpu_type, "cpu-type", &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]),
            base_hartid, "hartid-base", &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]),
            cpus_per_socket, "num-harts", &error_abort);
        object_property_set_bool(OBJECT(&s->soc[i]),
            true, "realized", &error_abort);

        /* Core Local Interruptor (timer and IPI) for each socket */
        sifive_clint_create(
            memmap[SPIKE_CLINT].base + i * memmap[SPIKE_CLINT].size,
            memmap[SPIKE_CLINT].size, base_hartid, cpus_per_socket,
            SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE, false);
    }

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.spike.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.spike.mrom",
                           memmap[SPIKE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_MROM].base,
                                mask_rom);

    riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                 memmap[SPIKE_DRAM].base,
                                 htif_symbol_callback);

    if (machine->kernel_filename) {
        uint64_t kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                                  htif_symbol_callback);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                  /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        0x00000000,
        memmap[SPIKE_DRAM].base,     /* start: .dword DRAM_BASE */
        0x00000000,
                                     /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SPIKE_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[SPIKE_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[SPIKE_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, mask_rom,
                 &s->soc[0].harts[0].env, serial_hd(0));
}

static void spike_v1_10_0_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = spike_memmap;

    SpikeState *s = g_new0(SpikeState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    int i;
    unsigned int smp_cpus = machine->smp.cpus;

    s->num_socs = 1;

    if (!qtest_enabled()) {
        info_report("The Spike v1.10.0 machine has been deprecated. "
                    "Please use the generic spike machine and specify the ISA "
                    "versions using -cpu.");
    }

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc",
                            &s->soc[0], sizeof(s->soc[0]),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->soc[0]), SPIKE_V1_10_0_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc[0]), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc[0]), true, "realized",
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.spike.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.spike.mrom",
                           memmap[SPIKE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_MROM].base,
                                mask_rom);

    if (machine->kernel_filename) {
        riscv_load_kernel(machine->kernel_filename, htif_symbol_callback);
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                  /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        0x00000000,
        memmap[SPIKE_DRAM].base,     /* start: .dword DRAM_BASE */
        0x00000000,
                                     /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SPIKE_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[SPIKE_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[SPIKE_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, mask_rom,
                 &s->soc[0].harts[0].env, serial_hd(0));

    /* Core Local Interruptor (timer and IPI) */
    sifive_clint_create(memmap[SPIKE_CLINT].base, memmap[SPIKE_CLINT].size,
        0, smp_cpus, SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE,
        false);
}

static void spike_v1_09_1_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = spike_memmap;

    SpikeState *s = g_new0(SpikeState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    int i;
    unsigned int smp_cpus = machine->smp.cpus;

    s->num_socs = 1;

    if (!qtest_enabled()) {
        info_report("The Spike v1.09.1 machine has been deprecated. "
                    "Please use the generic spike machine and specify the ISA "
                    "versions using -cpu.");
    }

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc",
                            &s->soc[0], sizeof(s->soc[0]),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->soc[0]), SPIKE_V1_09_1_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc[0]), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc[0]), true, "realized",
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.spike.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_DRAM].base,
        main_mem);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.spike.mrom",
                           memmap[SPIKE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_MROM].base,
                                mask_rom);

    if (machine->kernel_filename) {
        riscv_load_kernel(machine->kernel_filename, htif_symbol_callback);
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x297 + memmap[SPIKE_DRAM].base - memmap[SPIKE_MROM].base, /* lui */
        0x00028067,                   /* jump to DRAM_BASE */
        0x00000000,                   /* reserved */
        memmap[SPIKE_MROM].base + sizeof(reset_vec), /* config string pointer */
        0, 0, 0, 0                    /* trap vector */
    };

    /* part one of config string - before memory size specified */
    const char *config_string_tmpl =
        "platform {\n"
        "  vendor ucb;\n"
        "  arch spike;\n"
        "};\n"
        "rtc {\n"
        "  addr 0x%" PRIx64 "x;\n"
        "};\n"
        "ram {\n"
        "  0 {\n"
        "    addr 0x%" PRIx64 "x;\n"
        "    size 0x%" PRIx64 "x;\n"
        "  };\n"
        "};\n"
        "core {\n"
        "  0" " {\n"
        "    " "0 {\n"
        "      isa %s;\n"
        "      timecmp 0x%" PRIx64 "x;\n"
        "      ipi 0x%" PRIx64 "x;\n"
        "    };\n"
        "  };\n"
        "};\n";

    /* build config string with supplied memory size */
    char *isa = riscv_isa_string(&s->soc[0].harts[0]);
    char *config_string = g_strdup_printf(config_string_tmpl,
        (uint64_t)memmap[SPIKE_CLINT].base + SIFIVE_TIME_BASE,
        (uint64_t)memmap[SPIKE_DRAM].base,
        (uint64_t)ram_size, isa,
        (uint64_t)memmap[SPIKE_CLINT].base + SIFIVE_TIMECMP_BASE,
        (uint64_t)memmap[SPIKE_CLINT].base + SIFIVE_SIP_BASE);
    g_free(isa);
    size_t config_string_len = strlen(config_string);

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SPIKE_MROM].base, &address_space_memory);

    /* copy in the config string */
    rom_add_blob_fixed_as("mrom.reset", config_string, config_string_len,
                          memmap[SPIKE_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, mask_rom,
                 &s->soc[0].harts[0].env, serial_hd(0));

    /* Core Local Interruptor (timer and IPI) */
    sifive_clint_create(memmap[SPIKE_CLINT].base, memmap[SPIKE_CLINT].size,
        0, smp_cpus, SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE,
        false);

    g_free(config_string);
}

static void spike_v1_09_1_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Spike Board (Privileged ISA v1.9.1)";
    mc->init = spike_v1_09_1_board_init;
    mc->max_cpus = 1;
}

static void spike_v1_10_0_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Spike Board (Privileged ISA v1.10)";
    mc->init = spike_v1_10_0_board_init;
    mc->max_cpus = 1;
}

DEFINE_MACHINE("spike_v1.9.1", spike_v1_09_1_machine_init)
DEFINE_MACHINE("spike_v1.10", spike_v1_10_0_machine_init)

static bool spike_get_multi_socket(Object *obj, Error **errp)
{
    SpikeState *s = SPIKE_MACHINE(obj);

    return s->multi_socket;
}

static void spike_set_multi_socket(Object *obj, bool value, Error **errp)
{
    SpikeState *s = SPIKE_MACHINE(obj);

    s->multi_socket = value;
}

static void spike_machine_instance_init(Object *obj)
{
    SpikeState *s = SPIKE_MACHINE(obj);

    /*
     * Multi-socket is disabled by default so users have to
     * explicitly enable it from command-line.
     */
    s->multi_socket = false;
    object_property_add_bool(obj, "multi-socket",
                             spike_get_multi_socket,
                             spike_set_multi_socket);
    object_property_set_description(obj, "multi-socket",
                                    "Set on/off to enable/disable the "
                                    "multi-socket support");
}

static void spike_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Spike board";
    mc->init = spike_board_init;
    mc->max_cpus = SPIKE_CPUS_MAX;
    mc->is_default = true;
    mc->default_cpu_type = SPIKE_V1_10_0_CPU;
}

static const TypeInfo spike_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("spike"),
    .parent     = TYPE_MACHINE,
    .class_init = spike_machine_class_init,
    .instance_init = spike_machine_instance_init,
    .instance_size = sizeof(SpikeState),
};

static void spike_machine_init_register_types(void)
{
    type_register_static(&spike_machine_typeinfo);
}

type_init(spike_machine_init_register_types)
