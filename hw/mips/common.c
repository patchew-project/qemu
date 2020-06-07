/*
 *  Common MIPS routines
 *
 *  Copyright (C) 2020  Huacai Chen <chenhc@lemote.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <linux/kvm.h>
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/boards.h"
#include "hw/mips/mips.h"
#include "sysemu/kvm_int.h"

#ifndef CONFIG_KVM

int mips_kvm_type(MachineState *machine, const char *vm_type)
{
    return 0;
}

#else

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

#endif
