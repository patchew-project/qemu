/*
 * x86 HVF CPU type initialization
 *
 * Copyright 2021 SUSE LLC
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
#include "hw/core/accel-cpu.h"

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

static void hvf_cpu_xsave_init(void)
{
    int i;

    /*
     * The allocated storage must be large enough for all of the
     * possible XSAVE state components.
     */
    assert(hvf_get_supported_cpuid(0xd, 0, R_ECX) <= 4096);

    /* x87 state is in the legacy region of the XSAVE area. */
    x86_ext_save_areas[XSTATE_FP_BIT].offset = 0;
    /* SSE state is in the legacy region of the XSAVE area. */
    x86_ext_save_areas[XSTATE_SSE_BIT].offset = 0;

    for (i = XSTATE_SSE_BIT + 1; i < XSAVE_STATE_AREA_COUNT; i++) {
        ExtSaveArea *esa = &x86_ext_save_areas[i];

        if (esa->size) {
            int sz = hvf_get_supported_cpuid(0xd, i, R_EAX);

            if (sz != 0) {
                assert(esa->size == sz);

                esa->offset = hvf_get_supported_cpuid(0xd, i, R_EBX);
                fprintf(stderr, "%s: state area %d: offset 0x%x, size 0x%x\n",
                        __func__, i, esa->offset, esa->size);
            }
        }
    }
}

static void hvf_cpu_instance_init(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    host_cpu_instance_init(cpu);

    /* Special cases not set in the X86CPUDefinition structs: */
    /* TODO: in-kernel irqchip for hvf */

    if (cpu->max_features) {
        hvf_cpu_max_instance_init(cpu);
    }

    hvf_cpu_xsave_init();
}

static void hvf_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_realizefn = host_cpu_realizefn;
    acc->cpu_instance_init = hvf_cpu_instance_init;
}

static const TypeInfo hvf_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("hvf"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = hvf_cpu_accel_class_init,
    .abstract = true,
};

static void hvf_cpu_accel_register_types(void)
{
    type_register_static(&hvf_cpu_accel_type_info);
}

type_init(hvf_cpu_accel_register_types);
