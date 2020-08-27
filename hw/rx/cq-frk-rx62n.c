/*
 * CQ publishing CQ-FRK-RX62N
 *
 * Copyright (c) 2020 Yoshinori Sato
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/rx/loader.h"
#include "hw/qdev-properties.h"
#include "hw/rx/rx62n.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"

typedef struct {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    RX62NState mcu;
} FRK_RX62NMachineState;

#define TYPE_FRK_RX62N_MACHINE MACHINE_TYPE_NAME("cq-frk-rx62n")

#define FRK_RX62N_MACHINE(obj) \
    OBJECT_CHECK(FRK_RX62NMachineState, (obj), TYPE_FRK_RX62N_MACHINE)

static void frk_rx62n_init(MachineState *machine)
{
    FRK_RX62NMachineState *s = FRK_RX62N_MACHINE(machine);
    RX62NClass *rx62nc;
    MemoryRegion *sysmem = get_system_memory();

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu",
                            &s->mcu, TYPE_R5F562N7_MCU);
    rx62nc = RX62N_MCU_GET_CLASS(&s->mcu);
    object_property_set_link(OBJECT(&s->mcu), "main-bus", OBJECT(sysmem),
                             &error_abort);
    object_property_set_uint(OBJECT(&s->mcu), "xtal-frequency-hz",
                             12 * 1000 * 1000, &error_abort);
    if (bios_name) {
        if (!load_bios(bios_name, rx62nc->rom_flash_size, &error_abort)) {
            exit(0);
        }
    } else if (!qtest_enabled()) {
        error_report("No bios specified");
        exit(1);
    }
    qdev_realize(DEVICE(&s->mcu), NULL, &error_abort);
}

static void frk_rx62n_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CQ publishing CQ-FRK-RX62N";
    mc->init = frk_rx62n_init;
    mc->is_default = 0;
    mc->default_cpu_type = TYPE_RX62N_CPU;
}

static const TypeInfo frk_rx62n_type = {
    .name = MACHINE_TYPE_NAME("cq-frk-rx62n"),
    .parent = TYPE_MACHINE,
    .instance_size  = sizeof(FRK_RX62NMachineState),
    .class_init = frk_rx62n_class_init,
};

static void frk_rx62n_machine_init(void)
{
    type_register_static(&frk_rx62n_type);
}

type_init(frk_rx62n_machine_init)
