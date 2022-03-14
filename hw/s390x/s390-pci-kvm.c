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

#include <linux/kvm.h>

#include "kvm/kvm_s390x.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-kvm.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/s390x/s390-pci-vfio.h"

bool s390_pci_kvm_zpciop_allowed(void)
{
    return s390_has_feat(S390_FEAT_ZPCI_INTERP) && kvm_s390_get_zpci_op();
}

int s390_pci_kvm_plug(S390PCIBusDevice *pbdev)
{
    int rc;

    struct kvm_s390_zpci_op args = {
        .op = KVM_S390_ZPCIOP_INIT
    };

    if (!s390_pci_get_host_fh(pbdev, &args.fh)) {
        return -EINVAL;
    }

    rc = kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
    if (!rc) {
        /*
         * The host device is already in an enabled state, but we always present
         * the initial device state to the guest as disabled (ZPCI_FS_DISABLED).
         * Therefore, mask off the enable bit from the passthrough handle until
         * the guest issues a CLP SET PCI FN later to enable the device.
         */
        pbdev->fh = (args.newfh & ~FH_MASK_ENABLE);
    }

    return rc;
}

int s390_pci_kvm_unplug(S390PCIBusDevice *pbdev)
{
    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh | FH_MASK_ENABLE,
        .op = KVM_S390_ZPCIOP_END
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
}

int s390_pci_kvm_interp_enable(S390PCIBusDevice *pbdev)
{
    uint32_t fh;
    int rc;

    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh | FH_MASK_ENABLE,
        .op = KVM_S390_ZPCIOP_START_INTERP
    };

 retry:
    rc = kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);

    if (rc == -ENODEV) {
        /*
         * If the function wasn't found, re-sync the function handle with vfio
         * and if a change is detected, retry the operation with the new fh.
         * This can happen while the device is disabled to the guest due to
         * vfio-triggered events (e.g. vfio hot reset for ISM during plug)
         */
        if (!s390_pci_get_host_fh(pbdev, &fh)) {
            return -EINVAL;
        }
        if (fh != args.fh) {
            args.fh = fh;
            goto retry;
        }
    }
    if (!rc) {
        pbdev->fh = args.newfh;
    }

    return rc;
}

int s390_pci_kvm_interp_disable(S390PCIBusDevice *pbdev)
{
    int rc;

    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh,
        .op = KVM_S390_ZPCIOP_STOP_INTERP
    };

    rc = kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
    if (!rc) {
        pbdev->fh = args.newfh;
    }

    return rc;
}

int s390_pci_kvm_aif_enable(S390PCIBusDevice *pbdev, ZpciFib *fib, bool assist)
{
    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh,
        .op = KVM_S390_ZPCIOP_REG_INT,
        .u.reg_int.ibv = fib->aibv,
        .u.reg_int.sb = fib->aisb,
        .u.reg_int.noi = FIB_DATA_NOI(fib->data),
        .u.reg_int.isc = FIB_DATA_ISC(fib->data),
        .u.reg_int.sbo = FIB_DATA_AISBO(fib->data),
        .u.reg_int.flags = (assist) ? 0 : KVM_S390_ZPCIOP_REGINT_HOST
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
}

int s390_pci_kvm_aif_disable(S390PCIBusDevice *pbdev)
{
    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh,
        .op = KVM_S390_ZPCIOP_DEREG_INT
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
}
