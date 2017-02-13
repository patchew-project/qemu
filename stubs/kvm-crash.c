#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/kvm.h"

void kvm_arch_save_crash_info(CPUState *cs)
{
    return;
}
