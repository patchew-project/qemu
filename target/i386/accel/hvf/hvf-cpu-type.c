/*
 * x86 HVF CPU type initialization
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "host-cpu.h"
#include "hvf-cpu-type.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/hvf.h"


static void hvf_cpu_common_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = host_cpu_realizefn;
}

static void hvf_cpu_max_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;

    host_cpu_max_initfn(cpu);

    env->cpuid_min_level =
        hvf_get_supported_cpuid(0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        hvf_get_supported_cpuid(0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        hvf_get_supported_cpuid(0xC0000000, 0, R_EAX);
}

static void hvf_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);

    host_cpu_initfn(obj);

    /* Special cases not set in the X86CPUDefinition structs: */
    /* TODO: in-kernel irqchip for hvf */

    if (cpu->max_features) {
        hvf_cpu_max_initfn(obj);
    }
}

static const TypeInfo hvf_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("hvf"),
    .parent = TYPE_X86_CPU,

    .instance_init = hvf_cpu_initfn,
    .class_init = hvf_cpu_common_class_init,
};

static void hvf_cpu_register_base_type(void)
{
    type_register_static(&hvf_cpu_type_info);
}

type_init(hvf_cpu_register_base_type);

void hvf_cpu_type_init(void)
{
    if (hvf_enabled()) {
        x86_cpu_register_cpu_models(X86_CPU_TYPE_NAME("hvf"));
    }
}

accel_cpu_init(hvf_cpu_type_init);
