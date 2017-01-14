/*
 * Empty machine
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "elf.h"

#ifdef TARGET_WORDS_BIGENDIAN
#define LOAD_ELF_ENDIAN_FLAG 1
#else
#define LOAD_ELF_ENDIAN_FLAG 0
#endif

static hwaddr cpu_initial_pc;

static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    return cpu_get_phys_page_debug(CPU(opaque), addr);
}

static void machine_none_load_kernel(CPUState *cpu, const char *kernel_fname,
                                     ram_addr_t ram_size)
{
    uint64_t elf_entry;
    int kernel_size;

    if (!ram_size) {
        error_report("You need RAM for loading a kernel");
        return;
    }

    kernel_size = load_elf(kernel_fname, translate_phys_addr, cpu, &elf_entry,
                           NULL, NULL, LOAD_ELF_ENDIAN_FLAG, EM_NONE, 0, 0);
    cpu_initial_pc = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(kernel_fname, &cpu_initial_pc, NULL, NULL,
                                  NULL, NULL);
    }
    if (kernel_size < 0) {
        kernel_size = load_image_targphys(kernel_fname, 0, ram_size);
    }
    if (kernel_size < 0) {
        error_report("Could not load kernel '%s'", kernel_fname);
        return;
    }
}

static void machine_none_cpu_reset(void *opaque)
{
    CPUState *cpu = CPU(opaque);

    cpu_reset(cpu);
    cpu_set_pc(cpu, cpu_initial_pc);
}

static void machine_none_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    MemoryRegion *ram;
    CPUState *cpu = NULL;

    /* Initialize CPU (if a model has been specified) */
    if (machine->cpu_model) {
        cpu = cpu_init(machine->cpu_model);
        if (!cpu) {
            error_report("Unable to initialize CPU");
            exit(1);
        }
        qemu_register_reset(machine_none_cpu_reset, cpu);
        cpu_reset(cpu);
    }

    /* RAM at address zero */
    if (ram_size) {
        ram = g_new(MemoryRegion, 1);
        memory_region_allocate_system_memory(ram, NULL, "ram", ram_size);
        memory_region_add_subregion(get_system_memory(), 0, ram);
    }

    if (machine->kernel_filename) {
        machine_none_load_kernel(cpu, machine->kernel_filename, ram_size);
    }
}

static void machine_none_machine_init(MachineClass *mc)
{
    mc->desc = "empty machine";
    mc->init = machine_none_init;
    mc->default_ram_size = 0;
}

DEFINE_MACHINE("none", machine_none_machine_init)
