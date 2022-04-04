/*
 * s390 zPCI KVM interfaces
 *
 * Copyright 2022 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"

#include "kvm/kvm_s390x.h"
#include "hw/s390x/s390-pci-kvm.h"
#include "cpu_models.h"

bool s390_pci_kvm_interp_allowed(void)
{
    return s390_has_feat(S390_FEAT_ZPCI_INTERP) && kvm_s390_get_zpci_op();
}
