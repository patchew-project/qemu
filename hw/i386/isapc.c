/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "hw/char/parallel-isa.h"
#include "hw/dma/i8257.h"
#include "hw/loader.h"
#include "hw/i386/pc.h"
#include "hw/ide/isa.h"
#include "hw/ide/ide-bus.h"
#include "system/kvm.h"
#include "hw/i386/kvm/clock.h"
#include "hw/xen/xen-x86.h"
#include "system/xen.h"
#include "hw/rtc/mc146818rtc.h"
#include "target/i386/cpu.h"

static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };


static void pc_init_isa(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *system_io = get_system_io();
    ISABus *isa_bus;
    GSIState *gsi_state;
    MemoryRegion *ram_memory;
    MemoryRegion *rom_memory = system_memory;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    int i;

    /*
     * There is no RAM split for the isapc machine
     */
    if (xen_enabled()) {
        xen_hvm_init_pc(pcms, &ram_memory);
    } else {
        ram_memory = machine->ram;

        pcms->max_ram_below_4g = 0xe0000000; /* default: 3.5G */
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = machine->ram_size;
    }

    x86_cpus_init(x86ms, pcmc->default_cpu_version);

    if (kvm_enabled()) {
        kvmclock_create(pcmc->kvmclock_create_always);
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(pcms, system_memory, rom_memory, 0);
    } else {
        assert(machine->ram_size == x86ms->below_4g_mem_size +
                                    x86ms->above_4g_mem_size);

        if (machine->kernel_filename != NULL) {
            /* For xen HVM direct kernel boot, load linux here */
            xen_load_linux(pcms);
        }
    }

    gsi_state = pc_gsi_create(&x86ms->gsi, false);

    isa_bus = isa_bus_new(NULL, system_memory, system_io,
                            &error_abort);
    isa_bus_register_input_irqs(isa_bus, x86ms->gsi);

    x86ms->rtc = isa_new(TYPE_MC146818_RTC);
    qdev_prop_set_int32(DEVICE(x86ms->rtc), "base_year", 2000);
    isa_realize_and_unref(x86ms->rtc, isa_bus, &error_fatal);

    i8257_dma_init(OBJECT(machine), isa_bus, 0);
    pcms->hpet_enabled = false;

    if (x86ms->pic == ON_OFF_AUTO_ON || x86ms->pic == ON_OFF_AUTO_AUTO) {
        pc_i8259_create(isa_bus, gsi_state->i8259_irq);
    }

    if (tcg_enabled()) {
        x86_register_ferr_irq(x86ms->gsi[13]);
    }

    pc_vga_init(isa_bus, NULL);

    /* init basic PC hardware */
    pc_basic_device_init(pcms, isa_bus, x86ms->gsi, x86ms->rtc,
                         !MACHINE_CLASS(pcmc)->no_floppy, 0x4);

    pc_nic_init(pcmc, isa_bus, NULL);

    ide_drive_get(hd, ARRAY_SIZE(hd));
    for (i = 0; i < MAX_IDE_BUS; i++) {
        ISADevice *dev;
        char busname[] = "ide.0";
        dev = isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i],
                           ide_irq[i],
                           hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
        /*
         * The ide bus name is ide.0 for the first bus and ide.1 for the
         * second one.
         */
        busname[4] = '0' + i;
        pcms->idebus[i] = qdev_get_child_bus(DEVICE(dev), busname);
    }
}

static void isapc_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    m->desc = "ISA-only PC";
    m->max_cpus = 1;
    m->option_rom_has_mr = true;
    m->rom_file_has_mr = false;
    pcmc->pci_enabled = false;
    pcmc->has_acpi_build = false;
    pcmc->smbios_defaults = false;
    pcmc->gigabyte_align = false;
    pcmc->smbios_legacy_mode = true;
    pcmc->has_reserved_memory = false;
    m->default_nic = "ne2k_isa";
    m->default_cpu_type = X86_CPU_TYPE_NAME("486");
    m->no_floppy = !module_object_class_by_name(TYPE_ISA_FDC);
    m->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
}

DEFINE_PC_MACHINE(isapc, "isapc", pc_init_isa,
                  isapc_machine_options);
