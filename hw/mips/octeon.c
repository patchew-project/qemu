/*
 * QEMU Cavium Octeon board model.
 *
 * Copyright (c) 2026 Kirill A. Korinsky
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/octeon_internal.h"
#include "target/mips/cpu.h"

#define TYPE_OCTEON_MACHINE MACHINE_TYPE_NAME("octeon3")
OBJECT_DECLARE_SIMPLE_TYPE(OcteonMachineState, OCTEON_MACHINE)

#define OCTEON_DEFAULT_CPU_HZ       1800000000
#define OCTEON_DEFAULT_REF_HZ       50000000
#define OCTEON_DEFAULT_IO_HZ        1000000000
#define OCTEON_DEFAULT_DDR_HZ       800000000
#define OCTEON_DEFAULT_RAM_SIZE     (1 * GiB)

struct OcteonMachineState {
    MachineState parent_obj;
    uint64_t cpu_hz;
    uint64_t ref_hz;
    uint64_t io_hz;
    uint64_t ddr_hz;
};

static void mips_octeon_init(MachineState *machine)
{
}

static void octeon_machine_instance_init(Object *obj)
{
    OcteonMachineState *oms = OCTEON_MACHINE(obj);

    oms->cpu_hz = OCTEON_DEFAULT_CPU_HZ;
    oms->ref_hz = OCTEON_DEFAULT_REF_HZ;
    oms->io_hz = OCTEON_DEFAULT_IO_HZ;
    oms->ddr_hz = OCTEON_DEFAULT_DDR_HZ;

    object_property_add_uint64_ptr(obj, "cpu-clock-hz", &oms->cpu_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "cpu-clock-hz",
                                    "CPU clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "ref-clock-hz", &oms->ref_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "ref-clock-hz",
                                    "Reference clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "io-clock-hz", &oms->io_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "io-clock-hz",
                                    "IO clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "ddr-clock-hz", &oms->ddr_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "ddr-clock-hz",
                                    "DDR clock frequency in Hz");
}

static void octeon_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Cavium Octeon III / EBB7304 EVK";
    mc->init = mips_octeon_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("OcteonCN73XX");
    mc->default_ram_size = OCTEON_DEFAULT_RAM_SIZE;
    mc->default_ram_id = "octeon.ram";
    mc->max_cpus = OCTEON_MAX_CPUS;
}

static const TypeInfo octeon_machine_types[] = {
    {
        .name = TYPE_OCTEON_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(OcteonMachineState),
        .instance_init = octeon_machine_instance_init,
        .class_init = octeon_machine_class_init,
    }
};

DEFINE_TYPES(octeon_machine_types)
