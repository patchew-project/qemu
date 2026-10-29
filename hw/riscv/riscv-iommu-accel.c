/*
 * RISC-V IOMMU - accelerated (nested) translation support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "trace.h"

#include "hw/pci/pci_bridge.h"
#include "hw/pci-host/gpex.h"
#include "hw/vfio/pci.h"
#include "hw/core/iommu.h"

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

static uint64_t riscv_iommu_accel_get_viommu_flags(void *opaque)
{
    /*
     * We return VIOMMU_FLAG_WANT_NESTING_PARENT to inform VFIO core to create a
     * nesting parent which is required for accelerated riscv iommu support.
     * The real HW nested support should be reported from host riscv iommu and if
     * it doesn't, the nesting parent allocation will fail anyway in VFIO core.
     */
    return VIOMMU_FLAG_WANT_NESTING_PARENT;
}

static uint32_t riscv_iommu_accel_ddtp_hwpt(RISCVIOMMUState *s,
                                            RISCVIOMMUAccelState *accel)
{
    unsigned mode = get_field(s->ddtp, RISCV_IOMMU_DDTP_MODE);
    /*
     * Initial proxy HWPT: with DDTP.Mode=OFF the IOMMU is
     * disabled and all accesses abort; any other mode (Bare or a
     * translation mode) starts with the bypass proxy until the guest
     * programs a device context.
     */
    return (mode == RISCV_IOMMU_DDTP_MODE_OFF) ?
               accel->abort_hwpt_id : accel->bypass_hwpt_id;
}

static bool riscv_iommu_accel_alloc_viommu(RISCVIOMMUState *s,
                                           HostIOMMUDeviceIOMMUFD *idev,
                                           Error **errp)
{
    RISCVIOMMUAccelState *accel = s->accel_state;
    struct iommu_hwpt_riscv bypass_data = {
        .dc[0] = RISCV_IOMMU_DC_TC_V,
        .dc[3] = RISCV_IOMMU_DC_FSC_MODE_BARE,
    };

    struct iommu_hwpt_riscv abort_data = {
        .dc[0] = 0,  /* V = 0, invalid DC aborts */
        .dc[3] = 0,  /* don't care */
    };

    uint32_t s2_hwpt_id = idev->hwpt_id;
    uint32_t viommu_id, hwpt_id;
    IOMMUFDViommu *viommu;

    if (!iommufd_backend_alloc_viommu(idev->iommufd, idev->devid,
                                      IOMMU_VIOMMU_TYPE_RISCV, s2_hwpt_id,
                                      NULL, 0, &viommu_id, errp)) {
        return false;
    }

    viommu = g_new0(IOMMUFDViommu, 1);
    viommu->viommu_id = viommu_id;
    viommu->s2_hwpt_id = s2_hwpt_id;
    viommu->iommufd = idev->iommufd;
    accel->viommu = viommu;

    /*
     * Pre-allocate HWPTs for the S1 bypass and abort cases, to be attached
     * later based on a guest device context (DC) or the DDTP mode.
     */
    if (!iommufd_backend_alloc_hwpt(idev->iommufd, idev->devid, viommu_id,
                                    0, IOMMU_HWPT_DATA_RISCV_S1,
                                    sizeof(abort_data), &abort_data,
                                    &accel->abort_hwpt_id, errp)) {
        goto free_viommu;
    }

    if (!iommufd_backend_alloc_hwpt(idev->iommufd, idev->devid, viommu_id,
                                    0, IOMMU_HWPT_DATA_RISCV_S1,
                                    sizeof(bypass_data), &bypass_data,
                                    &accel->bypass_hwpt_id, errp)) {
        goto free_abort_hwpt;
    }

    /* Attach a HWPT based on ddtp.iommu_mode value */
    hwpt_id = riscv_iommu_accel_ddtp_hwpt(s, accel);
    if (!host_iommu_device_iommufd_attach_hwpt(idev, IOMMU_NO_PASID, hwpt_id,
                                               errp)) {
        goto free_bypass_hwpt;
    }
    return true;

free_bypass_hwpt:
    iommufd_backend_free_id(idev->iommufd, accel->bypass_hwpt_id);
free_abort_hwpt:
    iommufd_backend_free_id(idev->iommufd, accel->abort_hwpt_id);
free_viommu:
    iommufd_backend_free_id(idev->iommufd, viommu->viommu_id);
    g_free(viommu);
    accel->viommu = NULL;
    return false;
}

static bool riscv_iommu_accel_set_iommu_device(PCIBus *bus, void *opaque,
                                               int devfn,
                                               HostIOMMUDevice *hiod,
                                               Error **errp)
{
    HostIOMMUDeviceIOMMUFD *idev = HOST_IOMMU_DEVICE_IOMMUFD(hiod);
    RISCVIOMMUState *s = (RISCVIOMMUState *) opaque;
    RISCVIOMMUAccelDevice *accel_dev = riscv_iommu_accel_get_dev(s, bus, devfn);

    if (!idev) {
        return true;
    }

    if (accel_dev->idev) {
        if (accel_dev->idev != idev) {
            error_setg(errp, "Device already has an associated idev 0x%x",
                       idev->devid);
            return false;
        }
        return true;
    }

    if (s->accel_state->viommu) {
        goto done;
    }

    if (!riscv_iommu_accel_alloc_viommu(s, idev, errp)) {
        error_append_hint(errp, "Unable to alloc vIOMMU: idev devid 0x%x: ",
                          idev->devid);
        return false;
    }

done:
    accel_dev->idev = idev;
    accel_dev->accel = s->accel_state;
    QLIST_INSERT_HEAD(&s->accel_state->device_list, accel_dev, next);
    trace_riscv_iommu_accel_set_iommu_device(devfn, idev->devid);
    return true;
}

static void riscv_iommu_accel_unset_iommu_device(PCIBus *bus, void *opaque,
                                                 int devfn)
{
    RISCVIOMMUState *s = (RISCVIOMMUState *) opaque;
    HostIOMMUDeviceIOMMUFD *idev;
    RISCVIOMMUAccelDevice *accel_dev;
    RISCVIOMMUAccelState *accel;
    RISCVIOMMUSpace *as;

    as = riscv_iommu_get_space(s, bus, devfn);
    if (!as) {
        return;
    }

    accel_dev = container_of(as, RISCVIOMMUAccelDevice, as);
    idev = accel_dev->idev;
    accel = accel_dev->accel;
    /* Re-attach the default s2 hwpt id */
    if (!host_iommu_device_iommufd_attach_hwpt(idev, IOMMU_NO_PASID,
                                               idev->hwpt_id, NULL)) {
        error_report("Unable to attach the default HW pagetable: idev devid "
                     "0x%x", idev->devid);
    }

    accel_dev->idev = NULL;
    accel_dev->accel = NULL;
    QLIST_REMOVE(accel_dev, next);
    trace_riscv_iommu_accel_unset_iommu_device(devfn, idev->devid);

    if (QLIST_EMPTY(&accel->device_list)) {
        iommufd_backend_free_id(accel->viommu->iommufd, accel->bypass_hwpt_id);
        iommufd_backend_free_id(accel->viommu->iommufd, accel->abort_hwpt_id);
        iommufd_backend_free_id(accel->viommu->iommufd, accel->viommu->viommu_id);
        g_free(accel->viommu);
        accel->viommu = NULL;
    }
}

static const PCIIOMMUOps riscv_iommu_accel_ops = {
    .supports_address_space = riscv_iommu_accel_supports_as,
    .get_address_space = riscv_iommu_accel_find_as,
    .get_viommu_flags = riscv_iommu_accel_get_viommu_flags,
    .set_iommu_device = riscv_iommu_accel_set_iommu_device,
    .unset_iommu_device = riscv_iommu_accel_unset_iommu_device,
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
    s->accel_state = g_new0(RISCVIOMMUAccelState, 1);
    s->iommu_ops = &riscv_iommu_accel_ops;
    riscv_iommu_accel_as_init(s);
}
