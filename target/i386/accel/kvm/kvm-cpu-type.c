/*
 * x86 KVM CPU type initialization
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "host-cpu.h"
#include "kvm-cpu-type.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"

#include "kvm_i386.h"

/* this information overloads the TYPE_X86_CPU type in x86-cpu.c */

static void kvm_cpu_realizefn(DeviceState *dev, Error **errp)
{
    X86CPU *cpu = X86_CPU(dev);
    CPUX86State *env = &cpu->env;

    /*
     * also for KVM the realize order is important, since
     * x86_cpu_realize() checks if nothing else has been set by the user,
     * or by the specialized x86 cpus (KVM, HVF) in
     * cpu->ucode_rev and cpu->phys_bits.
     *
     * So it's kvm_cpu -> host_cpu -> x86_cpu
     */
    if (cpu->max_features) {
        if (enable_cpu_pm && kvm_has_waitpkg()) {
            env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_WAITPKG;
        }
        if (cpu->ucode_rev == 0) {
            cpu->ucode_rev =
                kvm_arch_get_supported_msr_feature(kvm_state,
                                                   MSR_IA32_UCODE_REV);
        }
    }
    host_cpu_realizefn(dev, errp);
}

static void kvm_cpu_common_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = kvm_cpu_realizefn;
}

/*
 * KVM-specific features that are automatically added/removed
 * from all CPU models when KVM is enabled.
 */
static PropValue kvm_default_props[] = {
    { "kvmclock", "on" },
    { "kvm-nopiodelay", "on" },
    { "kvm-asyncpf", "on" },
    { "kvm-steal-time", "on" },
    { "kvm-pv-eoi", "on" },
    { "kvmclock-stable-bit", "on" },
    { "x2apic", "on" },
    { "acpi", "off" },
    { "monitor", "off" },
    { "svm", "off" },
    { NULL, NULL },
};

void x86_cpu_change_kvm_default(const char *prop, const char *value)
{
    PropValue *pv;
    for (pv = kvm_default_props; pv->prop; pv++) {
        if (!strcmp(pv->prop, prop)) {
            pv->value = value;
            break;
        }
    }

    /*
     * It is valid to call this function only for properties that
     * are already present in the kvm_default_props table.
     */
    assert(pv->prop);
}

static bool lmce_supported(void)
{
    uint64_t mce_cap = 0;

    if (kvm_ioctl(kvm_state, KVM_X86_GET_MCE_CAP_SUPPORTED, &mce_cap) < 0) {
        return false;
    }
    return !!(mce_cap & MCG_LMCE_P);
}

static void kvm_cpu_max_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    KVMState *s = kvm_state;

    host_cpu_max_initfn(cpu);

    if (lmce_supported()) {
        object_property_set_bool(OBJECT(cpu), "lmce", true, &error_abort);
    }

    env->cpuid_min_level =
        kvm_arch_get_supported_cpuid(s, 0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        kvm_arch_get_supported_cpuid(s, 0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        kvm_arch_get_supported_cpuid(s, 0xC0000000, 0, R_EAX);
}

static void kvm_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);

    host_cpu_initfn(obj);

    if (!kvm_irqchip_in_kernel()) {
        x86_cpu_change_kvm_default("x2apic", "off");
    }

    /* Special cases not set in the X86CPUDefinition structs: */

    x86_cpu_apply_props(cpu, kvm_default_props);

    if (cpu->max_features) {
        kvm_cpu_max_initfn(obj);
    }
}

static const TypeInfo kvm_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("kvm"),
    .parent = TYPE_X86_CPU,

    .instance_init = kvm_cpu_initfn,
    .class_init = kvm_cpu_common_class_init,
};

static void kvm_cpu_register_base_type(void)
{
    type_register_static(&kvm_cpu_type_info);
}

type_init(kvm_cpu_register_base_type);

void kvm_cpu_type_init(void)
{
    if (kvm_enabled()) {
        x86_cpu_register_cpu_models(X86_CPU_TYPE_NAME("kvm"));
        host_cpu_type_init();
    }
}

accel_cpu_init(kvm_cpu_type_init);
