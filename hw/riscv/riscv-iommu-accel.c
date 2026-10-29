/*
 * RISC-V IOMMU - accelerated (nested) translation support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "riscv-iommu.h"
#include "riscv-iommu-accel.h"

static RISCVIOMMUAccelDevice *riscv_iommu_accel_get_dev(RISCVIOMMUState *s,
                                                        PCIBus *bus, int devfn)
{
    RISCVIOMMUSpace *as;
    RISCVIOMMUAccelDevice *accel_dev;

    /* FIXME: for simplicity, assume only one iommu right now */
    as = riscv_iommu_get_space(s, bus, devfn);
    if (as) {
        return container_of(as, RISCVIOMMUAccelDevice, as);
    }

    accel_dev = g_new0(RISCVIOMMUAccelDevice, 1);
    as = &accel_dev->as;
    riscv_iommu_init_space(s, as, bus, devfn);

    return accel_dev;
}

static AddressSpace *riscv_iommu_accel_find_as(PCIBus *bus, void *opaque,
                                               int devfn)
{
    RISCVIOMMUState *s = (RISCVIOMMUState *) opaque;

    RISCVIOMMUAccelDevice *accel_dev = riscv_iommu_accel_get_dev(s, bus, devfn);
    RISCVIOMMUSpace *as = &accel_dev->as;

    return &as->iova_as;
}

static const PCIIOMMUOps riscv_iommu_accel_ops = {
    .get_address_space = riscv_iommu_accel_find_as,
};

void riscv_iommu_accel_init(RISCVIOMMUState *s)
{
    s->iommu_ops = &riscv_iommu_accel_ops;
}
