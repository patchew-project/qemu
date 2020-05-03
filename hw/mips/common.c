/*
 * Common MIPS routines
 *
 * Copyright (c) 2020 Huacai Chen (chenhc@lemote.com)
 * This code is licensed under the GNU GPL v2.
 */

#include <linux/kvm.h>
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/boards.h"
#include "hw/mips/mips.h"
#include "sysemu/kvm_int.h"

int mips_kvm_type(MachineState *machine, const char *vm_type)
{
    int r;
    KVMState *s = KVM_STATE(machine->accelerator);

    r = kvm_check_extension(s, KVM_CAP_MIPS_VZ);
    if (r > 0) {
        return KVM_VM_MIPS_VZ;
    }

    r = kvm_check_extension(s, KVM_CAP_MIPS_TE);
    if (r > 0) {
        return KVM_VM_MIPS_TE;
    }

    return -1;
}
