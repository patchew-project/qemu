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
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/hvf.h"

static void hvf_cpu_common_class_init(X86CPUClass *xcc)
{
    host_cpu_class_init(xcc);
}

static void hvf_cpu_max_instance_init(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    host_cpu_max_instance_init(cpu);

    env->cpuid_min_level =
        hvf_get_supported_cpuid(0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        hvf_get_supported_cpuid(0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        hvf_get_supported_cpuid(0xC0000000, 0, R_EAX);
}

static void hvf_cpu_instance_init(X86CPU *cpu)
{
    host_cpu_instance_init(cpu);

    /* Special cases not set in the X86CPUDefinition structs: */
    /* TODO: in-kernel irqchip for hvf */

    if (cpu->max_features) {
        hvf_cpu_max_instance_init(cpu);
    }
}

static const X86CPUAccel hvf_cpu_accel = {
    .name = TYPE_X86_CPU "-hvf",

    .realizefn = host_cpu_realizefn,
    .common_class_init = hvf_cpu_common_class_init,
    .instance_init = hvf_cpu_instance_init,
};

static void hvf_cpu_accel_init(void)
{
    if (hvf_enabled()) {
        x86_cpu_accel_init(&hvf_cpu_accel);
    }
}

accel_cpu_init(hvf_cpu_accel_init);
