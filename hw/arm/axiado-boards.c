/*
 * Axiado Boards
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/arm/axiado-boards.h"
#include "hw/arm/machines-qom.h"
#include "qemu/error-report.h"
#include "qom/object.h"

static void axiado_machine_init(MachineState *machine)
{
    AxiadoMachineState *ams = AXIADO_MACHINE(machine);
    AxiadoMachineClass *amc = AXIADO_MACHINE_GET_CLASS(machine);

    if (machine->ram_size > AX3000_RAM_SIZE_MAX) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08" PRIx64 ")",
                     machine->ram_size, AX3000_RAM_SIZE_MAX);
        exit(1);
    }

    ams->soc = AXIADO_SOC(object_new(amc->soc_type));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(ams->soc));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ams->soc), &error_fatal);

    DeviceState *card = qdev_new(TYPE_SD_CARD);
    DriveInfo *di = drive_get(IF_SD, 0, 0);
    qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(di), &error_fatal);
    qdev_realize_and_unref(card, qdev_get_child_bus(DEVICE(&ams->soc->sdhci0),
                           "sd-bus"), &error_fatal);
}

static const char *axiado_machine_get_default_cpu_type(const MachineState *ms)
{
    return ARM_CPU_TYPE_NAME("cortex-a53");
}

static void axiado_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AxiadoMachineClass *amc = AXIADO_MACHINE_CLASS(oc);

    mc->init = axiado_machine_init;
    mc->default_cpus = AX3000_NUM_CPUS;
    mc->min_cpus = AX3000_NUM_CPUS;
    mc->max_cpus = AX3000_NUM_CPUS;
    mc->get_default_cpu_type = axiado_machine_get_default_cpu_type;
    amc->soc_type = TYPE_AXIADO_SOC;
}

static const TypeInfo axiado_machine_types[] = {
    {
        .name          = TYPE_AXIADO_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(AxiadoMachineState),
        .class_size    = sizeof(AxiadoMachineClass),
        .class_init    = axiado_machine_class_init,
        .interfaces    = aarch64_machine_interfaces,
        .abstract      = true,
    }
};

DEFINE_TYPES(axiado_machine_types)
