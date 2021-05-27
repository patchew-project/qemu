/*
 * RX QEMU GDB simulator
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
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/loader.h"
#include "hw/rx/rx62n.h"
#include "sysemu/qtest.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "qom/object.h"

/* Same address of GDB integrated simulator */
#define SDRAM_BASE  EXT_CS_BASE

typedef struct RxGdbSimMachineClass RxGdbSimMachineClass;

struct RxGdbSimMachineClass {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
    const char *mcu_name;
    uint32_t xtal_freq_hz;
    size_t romsize;
};

struct RxGdbSimMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    RX62NState mcu;
};
typedef struct RxGdbSimMachineState RxGdbSimMachineState;

#define TYPE_RX_GDBSIM_MACHINE MACHINE_TYPE_NAME("rx62n-common")

DECLARE_OBJ_CHECKERS(RxGdbSimMachineState, RxGdbSimMachineClass,
                     RX_GDBSIM_MACHINE, TYPE_RX_GDBSIM_MACHINE)


#define TINYBOOT_TOP (0xffffff00)

static void set_bootstrap(hwaddr entry, hwaddr dtb)
{
    /* Minimal hardware initialize for kernel requirement */
    /* linux kernel only works little-endian mode */
    static uint8_t tinyboot[256] = {
        0xfb, 0x2e, 0x20, 0x00, 0x08,       /* mov.l #0x80020, r2 */
        0xf8, 0x2e, 0x00, 0x01, 0x01,       /* mov.l #0x00010100, [r2] */
        0xfb, 0x2e, 0x10, 0x00, 0x08,       /* mov.l #0x80010, r2 */
        0xf8, 0x22, 0xdf, 0x7d, 0xff, 0xff, /* mov.l #0xffff7ddf, [r2] */
        0x62, 0x42,                         /* add #4, r2 */
        0xf8, 0x22, 0xff, 0x7f, 0xff, 0x7f, /* mov.l #0x7fff7fff, [r2] */
        0xfb, 0x2e, 0x40, 0x82, 0x08,       /* mov.l #0x88240, r2 */
        0x3c, 0x22, 0x00,                   /* mov.b #0, 2[r2] */
        0x3c, 0x21, 0x4e,                   /* mov.b #78, 1[r2] */
        0xfb, 0x22, 0x70, 0xff, 0xff, 0xff, /* mov.l #0xffffff70, r2 */
        0xec, 0x21,                         /* mov.l [r2], r1 */
        0xfb, 0x22, 0x74, 0xff, 0xff, 0xff, /* mov.l #0xffffff74, r2 */
        0xec, 0x22,                         /* mov.l [r2], r2 */
        0x7f, 0x02,                         /* jmp r2 */
    };
    int i;

    *((uint32_t *)&tinyboot[0x70]) = cpu_to_le32(dtb);
    *((uint32_t *)&tinyboot[0x74]) = cpu_to_le32(entry);

    /* setup exception trap trampoline */
    for (i = 0; i < 31; i++) {
        *((uint32_t *)&tinyboot[0x80 + i * 4]) = cpu_to_le32(0x10 + i * 4);
    }
    *((uint32_t *)&tinyboot[0xfc]) = cpu_to_le32(TINYBOOT_TOP);
    rom_add_blob_fixed("tinyboot", tinyboot, sizeof(tinyboot), TINYBOOT_TOP);
}

static void load_kernel(const char *filename, uint32_t start, uint32_t size)
{
    long kernel_size;

    kernel_size = load_image_targphys(filename, start, size);
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", filename);
        exit(1);
    }
}

static void rx_gdbsim_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    RxGdbSimMachineState *s = RX_GDBSIM_MACHINE(machine);
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    const char *kernel_filename = machine->kernel_filename;
    const char *dtb_filename = machine->dtb;

    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be more than %s", sz);
        g_free(sz);
        exit(1);
    }

    /* Allocate memory space */
    memory_region_add_subregion(sysmem, SDRAM_BASE, machine->ram);

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rxc->mcu_name);
    object_property_set_link(OBJECT(&s->mcu), "main-bus", OBJECT(sysmem),
                             &error_abort);
    object_property_set_uint(OBJECT(&s->mcu), "xtal-frequency-hz",
                             rxc->xtal_freq_hz, &error_abort);
    /* Load kernel and dtb */
    if (kernel_filename) {
        ram_addr_t kernel_offset;
        ram_addr_t dtb_offset = 0;
        kernel_offset = machine->ram_size / 2;

        load_kernel(machine->kernel_filename,
                    SDRAM_BASE + kernel_offset, kernel_offset);
        if (dtb_filename) {
            int dtb_size;
            g_autofree void *dtb = load_device_tree(dtb_filename, &dtb_size);

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
        }
        set_bootstrap(SDRAM_BASE + kernel_offset, SDRAM_BASE + dtb_offset);
    } else {
        if (machine->firmware) {
            rom_add_file_fixed(machine->firmware, RX62N_CFLASH_BASE, 0);
        } else if (!qtest_enabled()) {
            error_report("No bios or kernel specified");
            exit(1);
        }
    }
    qdev_realize(DEVICE(&s->mcu), NULL, &error_abort);
}

static void rx_gdbsim_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = rx_gdbsim_init;
    mc->default_cpu_type = TYPE_RX62N_CPU;
    mc->default_ram_size = 16 * MiB;
    mc->default_ram_id = "ext-sdram";
}

static void rx62n7_class_init(ObjectClass *oc, void *data)
{
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    rxc->mcu_name = TYPE_R5F562N7_MCU;
    rxc->xtal_freq_hz = 12 * 1000 * 1000;
    mc->desc = "gdb simulator (R5F562N7 MCU and external RAM)";
};

static void rx62n8_class_init(ObjectClass *oc, void *data)
{
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    rxc->mcu_name = TYPE_R5F562N8_MCU;
    rxc->xtal_freq_hz = 12 * 1000 * 1000;
    mc->desc = "gdb simulator (R5F562N8 MCU and external RAM)";
};

static const TypeInfo rx_gdbsim_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("gdbsim-r5f562n7"),
        .parent         = TYPE_RX_GDBSIM_MACHINE,
        .class_init     = rx62n7_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("gdbsim-r5f562n8"),
        .parent         = TYPE_RX_GDBSIM_MACHINE,
        .class_init     = rx62n8_class_init,
    }, {
        .name           = TYPE_RX_GDBSIM_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(RxGdbSimMachineState),
        .class_size     = sizeof(RxGdbSimMachineClass),
        .class_init     = rx_gdbsim_class_init,
        .abstract       = true,
     }
};

DEFINE_TYPES(rx_gdbsim_types)
