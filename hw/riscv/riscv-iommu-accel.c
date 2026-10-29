/*
 * RISC-V IOMMU - accelerated (nested) translation support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/pci/pci_bridge.h"
#include "hw/pci-host/gpex.h"
#include "hw/vfio/pci.h"

#include "riscv-iommu.h"
#include "riscv-iommu-accel.h"

/*
 * The root region aliases the global system memory, and shared_as_sysmem
 * provides a shared Address Space referencing it. This Address Space is used
 * by all vfio-pci devices behind all accelerated IOMMU instances within a VM.
 */
static MemoryRegion root, sysmem;
static AddressSpace *shared_as_sysmem;

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

/*
 * Only allow PCIe bridges, pxb-pcie roots, and GPEX roots so vfio-pci
 * endpoints can sit downstream, and the RISC-V IOMMU PCI device
 * itself. Unlike ARM, the RISC-V IOMMU can be exposed as a PCI device;
 * it must stay usable in accelerated mode, but it is never translated
 * and is handed its target address space directly.
 * Accelerated IOMMU requires a vfio-pci endpoint using the iommufd
 * backend; all other device types are rejected. This avoids supporting
 * emulated endpoints, which would complicate IOTLB invalidation and
 * hurt performance.
 */
static bool riscv_iommu_accel_pdev_allowed(PCIDevice *pdev, bool *vfio_pci)
{
    if (object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE) ||
        object_dynamic_cast(OBJECT(pdev), TYPE_PXB_PCIE_DEV) ||
        object_dynamic_cast(OBJECT(pdev), TYPE_GPEX_ROOT_DEVICE) ||
        object_dynamic_cast(OBJECT(pdev), TYPE_RISCV_IOMMU_PCI)) {
        return true;
    } else if ((object_dynamic_cast(OBJECT(pdev), TYPE_VFIO_PCI))) {
        *vfio_pci = true;
        if (object_property_get_link(OBJECT(pdev), "iommufd", NULL)) {
            return true;
        }
    }
    return false;
}

static bool riscv_iommu_accel_supports_as(PCIBus *bus, void *opaque, int devfn,
                                          Error **errp)
{
    PCIDevice *pdev = pci_find_device(bus, pci_bus_num(bus), devfn);
    bool vfio_pci = false;

    if (pdev && !riscv_iommu_accel_pdev_allowed(pdev, &vfio_pci)) {
        if (vfio_pci) {
            error_setg(errp, "vfio-pci endpoint devices without an iommufd "
                       "backend not allowed when using riscv_iommu accel=on");
        } else {
            error_setg(errp, "Emulated endpoint devices are not allowed when "
                       "using riscv_iommu accel=on");
        }
        return false;
    }
    return true;
}

static AddressSpace *riscv_iommu_accel_find_as(PCIBus *bus, void *opaque,
                                               int devfn)
{
    RISCVIOMMUState *s = (RISCVIOMMUState *) opaque;
    PCIDevice *pdev = pci_find_device(bus, pci_bus_num(bus), devfn);

    /*
     * The riscv-iommu-pci itself is not a translated endpoint; hand out its
     * target address space as the default path does.
     */
    if (pdev && pci_is_iommu(pdev)) {
        return s->target_as;
    }

    RISCVIOMMUAccelDevice *accel_dev = riscv_iommu_accel_get_dev(s, bus, devfn);
    RISCVIOMMUSpace *as = &accel_dev->as;

    bool vfio_pci = false;

    if (pdev && !riscv_iommu_accel_pdev_allowed(pdev, &vfio_pci)) {
        /* Should never be here: supports_address_space() filters these out */
        g_assert_not_reached();
    }

    /*
     * In the accelerated mode, a vfio-pci device attached via the iommufd
     * backend must remain in the system address space. Such a device is
     * always translated by its physical IOMMU (using either stage-2-only
     * or nested S1+S2 translation), where the parent stage-2 page table
     * is allocated by the VFIO core to back the system address space.
     *
     * Return the shared_as_sysmem aliased to the global system memory in
     * this case. Sharing a single system address space also lets devices
     * behind different accelerated IOMMU instances in the same VM reuse a
     * single nesting parent HWPT in the VFIO core.
     *
     * For the remaining allowed devices, such as PCIe root ports and
     * bridges, which use the normal emulated translation path and software
     * IOTLBs, return the IOMMU address space.
     */
    if (vfio_pci) {
        return shared_as_sysmem;
    } else {
        return &as->iova_as;
    }
}

static const PCIIOMMUOps riscv_iommu_accel_ops = {
    .supports_address_space = riscv_iommu_accel_supports_as,
    .get_address_space = riscv_iommu_accel_find_as,
};

static void riscv_iommu_accel_as_init(RISCVIOMMUState *s)
{
    if (shared_as_sysmem) {
        return;
    }

    memory_region_init(&root, OBJECT(s), "root", UINT64_MAX);
    memory_region_init_alias(&sysmem, OBJECT(s), "riscv-iommu-accel-sysmem",
                             get_system_memory(), 0,
                             memory_region_size(get_system_memory()));
    memory_region_add_subregion(&root, 0, &sysmem);

    shared_as_sysmem = g_new0(AddressSpace, 1);
    address_space_init(shared_as_sysmem, &root, "riscv-iommu-accel-as-sysmem");
}

void riscv_iommu_accel_init(RISCVIOMMUState *s)
{
    s->iommu_ops = &riscv_iommu_accel_ops;
    riscv_iommu_accel_as_init(s);
}
