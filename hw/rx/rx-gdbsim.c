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
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/rx/loader.h"
#include "hw/rx/rx62n.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"

/* Same address of GDB integrated simulator */
#define SDRAM_BASE  EXT_CS_BASE

typedef struct RxGdbSimMachineClass {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
    const char *mcu_name;
    uint32_t xtal_freq_hz;
    size_t romsize;
} RxGdbSimMachineClass;

typedef struct RxGdbSimMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    RX62NState mcu;
} RxGdbSimMachineState;

#define TYPE_RX_GDBSIM_MACHINE MACHINE_TYPE_NAME("rx62n-common")

#define RX_GDBSIM_MACHINE(obj) \
    OBJECT_CHECK(RxGdbSimMachineState, (obj), TYPE_RX_GDBSIM_MACHINE)

#define RX_GDBSIM_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(RxGdbSimMachineClass, (klass), TYPE_RX_GDBSIM_MACHINE)
#define RX_GDBSIM_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RxGdbSimMachineClass, (obj), TYPE_RX_GDBSIM_MACHINE)

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
        *((uint32_t *)&tinyboot[0x40 + i * 4]) = cpu_to_le32(0x10 + i * 4);
    }
    *((uint32_t *)&tinyboot[0xfc - 0x40]) = cpu_to_le32(TINYBOOT_TOP);
    rom_add_blob_fixed("tinyboot", tinyboot, sizeof(tinyboot), TINYBOOT_TOP);
}

static void rx_gdbsim_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    RxGdbSimMachineState *s = RX_GDBSIM_MACHINE(machine);
    RxGdbSimMachineClass *rxc = RX_GDBSIM_MACHINE_GET_CLASS(machine);
    RX62NClass *rx62nc;
    MemoryRegion *sysmem = get_system_memory();
    const char *kernel_filename = machine->kernel_filename;
    const char *dtb_filename = machine->dtb;
    rx_kernel_info_t kernel_info;
    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be more than %s", sz);
        g_free(sz);
    }

    /* Allocate memory space */
    memory_region_add_subregion(sysmem, SDRAM_BASE, machine->ram);

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rxc->mcu_name);
    rx62nc = RX62N_MCU_GET_CLASS(&s->mcu);
    object_property_set_link(OBJECT(&s->mcu), "main-bus", OBJECT(sysmem),
                             &error_abort);
    object_property_set_uint(OBJECT(&s->mcu), "xtal-frequency-hz",
                             rxc->xtal_freq_hz, &error_abort);
    /* Load kernel and dtb */
    if (kernel_filename) {
        kernel_info.ram_start = SDRAM_BASE;
        kernel_info.ram_size = machine->ram_size;
        kernel_info.filename = kernel_filename;
        kernel_info.dtbname = dtb_filename;
        kernel_info.cmdline = machine->kernel_cmdline;
        if (!load_kernel(&kernel_info)) {
            exit(1);
        }
        set_bootstrap(kernel_info.entry, kernel_info.dtb_address);
    } else {
        if (bios_name) {
            if (!load_bios(bios_name, rx62nc->rom_flash_size, &error_abort)) {
                exit(0);
            }
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
