#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"

void kvm_arch_set_singlestep(CPUState *cpu, int enabled)
{
    warn_report("KVM does not support single stepping");
}
