/*
 * RX QEMU virtual platform
 *
 * Copyright (c) 2019 Yoshinori Sato
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/rx/rx62n.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"

/* Same address of GDB integrated simulator */
#define SDRAM_BASE 0x01000000

static void rx_load_image(RXCPU *cpu, const char *filename,
                          uint32_t start, uint32_t size)
{
    static uint32_t extable[32];
    long kernel_size;
    int i;

    kernel_size = load_image_targphys(filename, start, size);
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", filename);
        exit(1);
    }
    cpu->env.pc = start;

    /* setup exception trap trampoline */
    /* linux kernel only works little-endian mode */
    for (i = 0; i < ARRAY_SIZE(extable); i++) {
        extable[i] = cpu_to_le32(0x10 + i * 4);
    }
    rom_add_blob_fixed("extable", extable, sizeof(extable), 0xffffff80);
}

static void rxvirt_init(MachineState *machine)
{
    RX62NState *s = g_new(RX62NState, 1);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    const char *kernel_filename = machine->kernel_filename;
    const char *dtb_filename = machine->dtb;
    void *dtb = NULL;
    int dtb_size;
    ram_addr_t kernel_offset;
    ram_addr_t dtb_offset;
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (machine->ram_size < mc->default_ram_size) {
        error_report("Invalid RAM size, should be more than %" PRIi64 " Bytes",
                     mc->default_ram_size);
    }

    /* Allocate memory space */
    memory_region_init_ram(sdram, NULL, "rx-virt.sdram", machine->ram_size,
                           &error_fatal);
    memory_region_add_subregion(sysmem, SDRAM_BASE, sdram);

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu", s,
                            sizeof(RX62NState), TYPE_RX62N,
                            &error_fatal, NULL);
    object_property_set_link(OBJECT(s), OBJECT(sysmem),
                             "memory", &error_abort);
    object_property_set_bool(OBJECT(s), kernel_filename != NULL,
                             "load-kernel", &error_abort);
    object_property_set_bool(OBJECT(s), true, "realized", &error_abort);

    /* Load kernel and dtb */
    if (kernel_filename) {
        /*
         * The kernel image is loaded into
         * the latter half of the SDRAM space.
         */
        kernel_offset = machine->ram_size / 2;
        rx_load_image(RXCPU(first_cpu), kernel_filename,
                      SDRAM_BASE + kernel_offset, kernel_offset);
        if (dtb_filename) {
            dtb = load_device_tree(dtb_filename, &dtb_size);
            if (dtb == NULL) {
                error_report("Couldn't open dtb file %s", dtb_filename);
                exit(1);
            }
            if (machine->kernel_cmdline &&
                qemu_fdt_setprop_string(dtb, "/chosen", "bootargs",
                                        machine->kernel_cmdline) < 0) {
                error_report("Couldn't set /chosen/bootargs");
                exit(1);
            }
            /* DTB is located at the end of SDRAM space. */
            dtb_offset = machine->ram_size - dtb_size;
            rom_add_blob_fixed("dtb", dtb, dtb_size,
                               SDRAM_BASE + dtb_offset);
            /* Set dtb address to R1 */
            RXCPU(first_cpu)->env.regs[1] = SDRAM_BASE + dtb_offset;
        }
    }
}

static void rxvirt_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RX QEMU Virtual Target";
    mc->init = rxvirt_init;
    mc->is_default = 1;
    mc->default_cpu_type = TYPE_RX62N_CPU;
    mc->default_ram_size = 16 * MiB;
}

static const TypeInfo rxvirt_type = {
    .name = MACHINE_TYPE_NAME("rx-virt"),
    .parent = TYPE_MACHINE,
    .class_init = rxvirt_class_init,
};

static void rxvirt_machine_init(void)
{
    type_register_static(&rxvirt_type);
}

type_init(rxvirt_machine_init)
