#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"

bool kvm_arch_has_guest_debug_singlestep(CPUState *cs)
{
    /* for backwards compatibility assume the feature is present */
    return true;
}

void kvm_arch_set_singlestep(CPUState *cpu, int enabled)
{
    warn_report("KVM does not support single stepping");
}
