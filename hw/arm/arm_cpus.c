/*
 * ARM CPUs
 *
 * Copyright (c) 2022 Greensocs
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/arm_cpus.h"
#include "hw/cpu/cpus.h"
#include "hw/qdev-properties.h"
#include "cpu.h"

static Property arm_cpus_props[] = {
    /* FIXME: get the default values from the arm cpu object */
    DEFINE_PROP_BOOL("reset-hivecs", ArmCpusState, reset_hivecs, false),
    DEFINE_PROP_BOOL("has_el3", ArmCpusState, has_el3, false),
    DEFINE_PROP_BOOL("has_el2", ArmCpusState, has_el2, false),
    DEFINE_PROP_UINT64("reset-cbar", ArmCpusState, reset_cbar, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void arm_cpus_configure_cpu(CpusState *base, CPUState *cpu,
                                   unsigned i)
{
    ArmCpusState *s = ARM_CPUS(base);
    DeviceState *cpudev = DEVICE(cpu);

    qdev_prop_set_uint32(cpudev, "core-count", base->topology.cpus);
    qdev_prop_set_bit(cpudev, "reset-hivecs", s->reset_hivecs);
    qdev_prop_set_bit(cpudev, "has_el3", s->has_el3);
    qdev_prop_set_bit(cpudev, "has_el2", s->has_el2);
    qdev_prop_set_uint64(cpudev, "reset-cbar", s->reset_cbar);
}

static void arm_cpus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CpusClass *cc = CPUS_CLASS(klass);

    device_class_set_props(dc, arm_cpus_props);

    cc->configure_cpu = arm_cpus_configure_cpu;
    cc->base_cpu_type = TYPE_ARM_CPU;
}

static const TypeInfo arm_cpus_info = {
    .name              = TYPE_ARM_CPUS,
    .parent            = TYPE_CPUS,
    .instance_size     = sizeof(ArmCpusState),
    .class_init        = arm_cpus_class_init,
};

static void arm_cpus_register_types(void)
{
    type_register_static(&arm_cpus_info);
}

type_init(arm_cpus_register_types)
