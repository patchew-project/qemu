/*
 * RX QEMU virtual target
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
#include "exec/address-spaces.h"
#include "exec/cpu-all.h"

static void rxqemu_init(MachineState *machine)
{
    DeviceState *cpu;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    const char *kernel_filename = machine->kernel_filename;
    const char *dtb_filename = machine->dtb;
    void *dtb = NULL;
    int dtb_size;

    /* Allocate memory space */
    memory_region_init_ram(sdram, NULL, "rxqemu.sdram", 0x01000000,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0x01000000, sdram);

    cpu = qdev_create(NULL, TYPE_RX62N);
    object_property_set_link(OBJECT(cpu), OBJECT(get_system_memory()),
                             "memory", &error_abort);
    object_property_set_bool(OBJECT(cpu), kernel_filename != NULL,
                             "load-kernel", &error_abort);
    /* This will exit with an error if the user passed us a bad cpu_type */
    qdev_init_nofail(cpu);

    if (kernel_filename) {
        rx_load_image(RXCPU(first_cpu), kernel_filename,
                      0x01800000, 0x00800000);
    }
    if (dtb_filename) {
        dtb = load_device_tree(dtb_filename, &dtb_size);
        if (dtb == NULL) {
            fprintf(stderr, "Couldn't open dtb file %s\n", dtb_filename);
            exit(1);
        }
        if (machine->kernel_cmdline &&
            qemu_fdt_setprop_string(dtb, "/chosen", "bootargs",
                                    machine->kernel_cmdline) < 0) {
            fprintf(stderr, "couldn't set /chosen/bootargs\n");
            exit(1);
        }
        rom_add_blob_fixed("dtb", dtb, dtb_size, 0x02000000 - dtb_size);
        /* Set dtb address to R1 */
        RXCPU(first_cpu)->env.regs[1] = 0x02000000 - dtb_size;
    }
}

static void rxqemu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RX QEMU Virtual Target";
    mc->init = rxqemu_init;
    mc->is_default = 1;
    mc->default_cpu_type = TYPE_RXCPU;
}

static const TypeInfo rxqemu_type = {
    .name = MACHINE_TYPE_NAME("rx-qemu"),
    .parent = TYPE_MACHINE,
    .class_init = rxqemu_class_init,
};

static void rxqemu_machine_init(void)
{
    type_register_static(&rxqemu_type);
}

type_init(rxqemu_machine_init)
