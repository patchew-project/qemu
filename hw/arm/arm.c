/*
 * ARM mach-virt emulation
 * Copyright (c) 2013 Linaro Limited
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
 *
 * Emulate a virtual board which works by passing Linux all the information
 * it needs about what devices are present via the device tree.
 * There are some restrictions about what we can do here:
 *  + we can only present devices whose Linux drivers will work based
 *    purely on the device tree with no platform data at all
 * This is essentially the same approach kvmtool uses.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/arm.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/arm/sysbus-fdt.h"
#include "hw/platform-bus.h"
#include "hw/qdev-properties.h"
#include "hw/arm/fdt.h"
#include "hw/intc/arm_gic.h"
#include "kvm_arm.h"

static char *virt_get_gic_version(Object *obj, Error **errp)
{
    ArmMachineState *ams = ARM_MACHINE(obj);
    const char *val = ams->gic_version == 3 ? "3" : "2";

    return g_strdup(val);
}

static void virt_set_gic_version(Object *obj, const char *value, Error **errp)
{
    ArmMachineState *ams = ARM_MACHINE(obj);

    if (!strcmp(value, "3")) {
        ams->gic_version = 3;
    } else if (!strcmp(value, "2")) {
        ams->gic_version = 2;
    } else if (!strcmp(value, "host")) {
        ams->gic_version = 0; /* Will probe later */
    } else if (!strcmp(value, "max")) {
        ams->gic_version = -1; /* Will probe later */
    } else {
        error_setg(errp, "Invalid gic-version value");
        error_append_hint(errp, "Valid values are 3, 2, host, max.\n");
    }
}

static void arm_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    /* We know we will never create a pre-ARMv7 CPU which needs 1K pages */
    mc->minimum_page_bits = 12;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a15");
    mc->numa_mem_supported = true;
    mc->auto_enable_numa_with_memhp = true;
}

static void arm_instance_init(Object *obj)
{
    ArmMachineState *ams = ARM_MACHINE(obj);
    /* Default GIC type is v2 */
    ams->gic_version = 2;
    object_property_add_str(obj, "gic-version", virt_get_gic_version,
                        virt_set_gic_version, NULL);
    object_property_set_description(obj, "gic-version",
                                    "Set GIC version. "
                                    "Valid values are 2, 3 and host", NULL);

}

static const TypeInfo arm_machine_info = {
    .name          = TYPE_ARM_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(ArmMachineState),
    .class_size    = sizeof(ArmMachineClass),
    .class_init    = arm_machine_class_init,
    .instance_init = arm_instance_init,
    .interfaces = (InterfaceInfo[]) {
         { }
    },
};

static void macharm_machine_init(void)
{
    type_register_static(&arm_machine_info);
}
type_init(macharm_machine_init);
