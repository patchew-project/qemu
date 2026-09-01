/*
 * QEMU sPAPR PCI host for VFIO
 *
 * Copyright (c) 2011-2014 Alexey Kardashevskiy, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_device.h"
#include "hw/vfio/vfio-container-legacy.h"
#include "qemu/error-report.h"
#include "hw/vfio/pci.h"
#include "hw/ppc/spapr_vfio.h"

/*
 * Interfaces for IBM EEH (Enhanced Error Handling)
 */
static bool vfio_eeh_container_ok(VFIOLegacyContainer *container)
{
    /*
     * As of 2016-03-04 (linux-4.5) the host kernel EEH/VFIO
     * implementation is broken if there are multiple groups in a
     * container.  The hardware works in units of Partitionable
     * Endpoints (== IOMMU groups) and the EEH operations naively
     * iterate across all groups in the container, without any logic
     * to make sure the groups have their state synchronized.  For
     * certain operations (ENABLE) that might be ok, until an error
     * occurs, but for others (GET_STATE) it's clearly broken.
     */

    /*
     * XXX Once fixed kernels exist, test for them here
     */

    if (QLIST_EMPTY(&container->group_list)) {
        return false;
    }

    if (QLIST_NEXT(QLIST_FIRST(&container->group_list), container_next)) {
        return false;
    }

    return true;
}

static int vfio_eeh_container_op(VFIOLegacyContainer *container, uint32_t op)
{
    struct vfio_eeh_pe_op pe_op = {
        .argsz = sizeof(pe_op),
        .op = op,
    };
    int ret;

    if (!vfio_eeh_container_ok(container)) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x: "
                     "kernel requires a container with exactly one group", op);
        return -EPERM;
    }

    ret = ioctl(container->fd, VFIO_EEH_PE_OP, &pe_op);
    if (ret < 0) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x failed: %m", op);
        return -errno;
    }

    return ret;
}

static VFIOLegacyContainer *vfio_eeh_as_container(AddressSpace *as)
{
    VFIOAddressSpace *space = vfio_address_space_get(as);
    VFIOContainer *bcontainer = NULL;

    if (QLIST_EMPTY(&space->containers)) {
        /* No containers to act on */
        goto out;
    }

    bcontainer = QLIST_FIRST(&space->containers);

    if (QLIST_NEXT(bcontainer, next)) {
        /*
         * We don't yet have logic to synchronize EEH state across
         * multiple containers
         */
        bcontainer = NULL;
        goto out;
    }

out:
    vfio_address_space_put(space);
    return VFIO_IOMMU_LEGACY(bcontainer);
}

static bool vfio_eeh_as_ok(AddressSpace *as)
{
    VFIOLegacyContainer *container = vfio_eeh_as_container(as);

    return (container != NULL) && vfio_eeh_container_ok(container);
}

static int vfio_eeh_as_op(AddressSpace *as, uint32_t op)
{
    VFIOLegacyContainer *container = vfio_eeh_as_container(as);

    if (!container) {
        return -ENODEV;
    }
    return vfio_eeh_container_op(container, op);
}

bool spapr_phb_eeh_available(SpaprPhbState *sphb)
{
    return vfio_eeh_as_ok(&sphb->iommu_as);
}

void spapr_phb_vfio_eeh_reenable(SpaprPhbState *sphb)
{
    vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_ENABLE);
}

static void spapr_eeh_pci_find_device(PCIBus *bus, PCIDevice *pdev,
                                      void *opaque)
{
    bool *found = opaque;

    if (object_dynamic_cast(OBJECT(pdev), "vfio-pci")) {
        *found = true;
    }
}

int spapr_phb_vfio_eeh_set_option(SpaprPhbState *sphb,
                                  unsigned int addr, int option)
{
    uint32_t op;
    int ret;

    switch (option) {
    case RTAS_EEH_DISABLE:
        op = VFIO_EEH_PE_DISABLE;
        break;
    case RTAS_EEH_ENABLE: {
        PCIHostState *phb;
        bool found = false;

        /*
         * The EEH functionality is enabled per sphb level instead of
         * per PCI device. We have already identified this specific sphb
         * based on buid passed as argument to ibm,set-eeh-option rtas
         * call. Now we just need to check the validity of the PCI
         * pass-through devices (vfio-pci) under this sphb bus.
         * We have already validated that all the devices under this sphb
         * are from same iommu group (within same PE) before coming here.
         *
         * Prior to linux commit 98ba956f6a389 ("powerpc/pseries/eeh:
         * Rework device EEH PE determination") kernel would call
         * eeh-set-option for each device in the PE using the device's
         * config_address as the argument rather than the PE address.
         * Hence if we check validity of supplied config_addr whether
         * it matches to this PHB will cause issues with older kernel
         * versions v5.9 and older. If we return an error from
         * eeh-set-option when the argument isn't a valid PE address
         * then older kernels (v5.9 and older) will interpret that as
         * EEH not being supported.
         */
        phb = PCI_HOST_BRIDGE(sphb);
        pci_for_each_device(phb->bus, (addr >> 16) & 0xFF,
                            spapr_eeh_pci_find_device, &found);

        if (!found) {
            return RTAS_OUT_PARAM_ERROR;
        }

        op = VFIO_EEH_PE_ENABLE;
        break;
    }
    case RTAS_EEH_THAW_IO:
        op = VFIO_EEH_PE_UNFREEZE_IO;
        break;
    case RTAS_EEH_THAW_DMA:
        op = VFIO_EEH_PE_UNFREEZE_DMA;
        break;
    default:
        return RTAS_OUT_PARAM_ERROR;
    }

    ret = vfio_eeh_as_op(&sphb->iommu_as, op);
    if (ret < 0) {
        return RTAS_OUT_HW_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

int spapr_phb_vfio_eeh_get_state(SpaprPhbState *sphb, int *state)
{
    int ret;

    ret = vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_GET_STATE);
    if (ret < 0) {
        return RTAS_OUT_PARAM_ERROR;
    }

    *state = ret;
    return RTAS_OUT_SUCCESS;
}

static void spapr_phb_vfio_eeh_clear_dev_msix(PCIBus *bus,
                                              PCIDevice *pdev,
                                              void *opaque)
{
    /* Check if the device is VFIO PCI device */
    if (!object_dynamic_cast(OBJECT(pdev), "vfio-pci")) {
        return;
    }

    /*
     * The MSIx table will be cleaned out by reset. We need
     * disable it so that it can be reenabled properly. Also,
     * the cached MSIx table should be cleared as it's not
     * reflecting the contents in hardware.
     */
    if (msix_enabled(pdev)) {
        uint16_t flags;

        flags = pci_host_config_read_common(pdev,
                                            pdev->msix_cap + PCI_MSIX_FLAGS,
                                            pci_config_size(pdev), 2);
        flags &= ~PCI_MSIX_FLAGS_ENABLE;
        pci_host_config_write_common(pdev,
                                     pdev->msix_cap + PCI_MSIX_FLAGS,
                                     pci_config_size(pdev), flags, 2);
    }

    msix_reset(pdev);
}

static void spapr_phb_vfio_eeh_clear_bus_msix(PCIBus *bus, void *opaque)
{
       pci_for_each_device_under_bus(bus, spapr_phb_vfio_eeh_clear_dev_msix,
                                     NULL);
}

static void spapr_phb_vfio_eeh_pre_reset(SpaprPhbState *sphb)
{
       PCIHostState *phb = PCI_HOST_BRIDGE(sphb);

       pci_for_each_bus(phb->bus, spapr_phb_vfio_eeh_clear_bus_msix, NULL);
}

int spapr_phb_vfio_eeh_reset(SpaprPhbState *sphb, int option)
{
    uint32_t op;
    int ret;

    switch (option) {
    case RTAS_SLOT_RESET_DEACTIVATE:
        op = VFIO_EEH_PE_RESET_DEACTIVATE;
        break;
    case RTAS_SLOT_RESET_HOT:
        spapr_phb_vfio_eeh_pre_reset(sphb);
        op = VFIO_EEH_PE_RESET_HOT;
        break;
    case RTAS_SLOT_RESET_FUNDAMENTAL:
        spapr_phb_vfio_eeh_pre_reset(sphb);
        op = VFIO_EEH_PE_RESET_FUNDAMENTAL;
        break;
    default:
        return RTAS_OUT_PARAM_ERROR;
    }

    ret = vfio_eeh_as_op(&sphb->iommu_as, op);
    if (ret < 0) {
        return RTAS_OUT_HW_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

int spapr_phb_vfio_eeh_configure(SpaprPhbState *sphb)
{
    int ret;

    ret = vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_CONFIGURE);
    if (ret < 0) {
        return RTAS_OUT_PARAM_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

typedef struct SpaprVFIOErrinjctBarMatch {
    uint64_t guest_addr;

    PCIDevice *pdev;
    VFIOPCIDevice *vdev;
    int bar;

    uint64_t guest_bar_start;
    uint64_t bar_size;
    uint64_t offset;
} SpaprVFIOErrinjctBarMatch;

static VFIOPCIDevice *spapr_vfio_errinjct_pci_to_vfio(PCIDevice *pdev)
{
    if (!object_dynamic_cast(OBJECT(pdev), TYPE_VFIO_PCI)) {
        return NULL;
    }

    return container_of(pdev, VFIOPCIDevice, parent_obj);
}

static void spapr_vfio_errinjct_find_bar_cb(PCIBus *bus,
                                            PCIDevice *pdev,
                                            void *opaque)
{
    SpaprVFIOErrinjctBarMatch *ctx = opaque;
    VFIOPCIDevice *vdev;
    int bar;

    if (ctx->pdev) {
        return;
    }

    vdev = spapr_vfio_errinjct_pci_to_vfio(pdev);
    if (!vdev) {
        return;
    }

    for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
        pcibus_t guest_bar_start;
        uint64_t bar_size;
        uint64_t offset;

        bar_size = vdev->bars[bar].region.size;
        if (!bar_size) {
            continue;
        }

        guest_bar_start = pci_get_bar_addr(pdev, bar);
        if (guest_bar_start == PCI_BAR_UNMAPPED) {
            continue;
        }

        if (ctx->guest_addr < guest_bar_start ||
            ctx->guest_addr - guest_bar_start >= bar_size) {
            if (vdev->bars[bar].mem64) {
                bar++;
            }
            continue;
        }

        offset = ctx->guest_addr - guest_bar_start;

        ctx->pdev = pdev;
        ctx->vdev = vdev;
        ctx->bar = bar;
        ctx->guest_bar_start = guest_bar_start;
        ctx->bar_size = bar_size;
        ctx->offset = offset;

        return;
    }
}

static int spapr_vfio_errinjct_get_host_bar(VFIOPCIDevice *vdev,
                                            int bar,
                                            uint64_t *host_bar_start)
{
    g_autofree char *path = NULL;
    g_autofree char *contents = NULL;
    char *line;
    char *saveptr = NULL;
    unsigned long long start;
    unsigned long long end;
    unsigned long long flags;
    int i;

    if (!vdev || !host_bar_start || bar < 0 || bar >= PCI_STD_NUM_BARS) {
        return -EINVAL;
    }

    /*
     * Read the host Linux sysfs resource file for the BAR.  Do not use a
     * VFIO PCI config-space read because that may return the
     * guest-programmed BAR value rather than the host resource address.
     */
    path = g_strdup_printf("/sys/bus/pci/devices/%04x:%02x:%02x.%u/resource",
                           vdev->host.domain,
                           vdev->host.bus,
                           vdev->host.slot,
                           vdev->host.function);

    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        error_report("vfio/eeh errinjct: failed to read %s", path);
        return -ENOENT;
    }

    line = strtok_r(contents, "\n", &saveptr);

    for (i = 0; line; i++, line = strtok_r(NULL, "\n", &saveptr)) {
        if (i != bar) {
            continue;
        }

        if (sscanf(line, "%llx %llx %llx", &start, &end, &flags) != 3) {
            error_report("vfio/eeh errinjct: malformed %s BAR%d",
                         path, bar);
            return -EINVAL;
        }

        if (!start || end < start) {
            error_report("vfio/eeh errinjct: invalid host resource BAR%d "
                         "start=0x%llx end=0x%llx flags=0x%llx",
                         bar, start, end, flags);
            return -EINVAL;
        }

        *host_bar_start = start;
        return 0;
    }

    return -EINVAL;
}

int spapr_phb_vfio_translate_errinjct_addr(SpaprPhbState *sphb,
                                           uint32_t config_addr,
                                           uint64_t guest_addr,
                                           uint64_t *host_pci_bus_addr)
{
    PCIHostState *phb;
    SpaprVFIOErrinjctBarMatch ctx = {
        .guest_addr = guest_addr,
        .pdev       = NULL,
        .vdev       = NULL,
        .bar        = -1,
    };
    uint64_t host_bar_start;
    int rc;

    if (!sphb || !host_pci_bus_addr) {
        return -EINVAL;
    }

    phb = PCI_HOST_BRIDGE(sphb);

    /*
     * BUID has already selected @sphb before this helper is called.
     * config_addr is retained for logging/debug only.  Do not rely on it
     * to find the target device; some RTAS IOA buffers may not carry a
     * valid guest BDF-style config address.  Scan VFIO BARs under this
     * PHB and match guest_addr against the guest BAR layout instead.
     */
    pci_for_each_device_under_bus(phb->bus,
                                  spapr_vfio_errinjct_find_bar_cb,
                                  &ctx);

    if (!ctx.pdev) {
        error_report("vfio/eeh errinjct: guest addr 0x%" PRIx64
                     " not within any VFIO BAR under BUID=0x%016" PRIx64
                     " config_addr=0x%08x",
                     guest_addr, sphb->buid, config_addr);
        return -ENODEV;
    }

    rc = spapr_vfio_errinjct_get_host_bar(ctx.vdev, ctx.bar, &host_bar_start);
    if (rc) {
        error_report("vfio/eeh errinjct: failed to get host BAR%d for "
                     "dev=%s BUID=0x%016" PRIx64 " config_addr=0x%08x rc=%d",
                     ctx.bar, ctx.pdev->name, sphb->buid, config_addr, rc);
        return rc;
    }

    if (host_bar_start == ctx.guest_bar_start) {
        error_report("vfio/eeh errinjct: refusing guest BAR as host BAR: "
                     "dev=%s BAR%d guest_bar=0x%" PRIx64
                     " host_bar=0x%" PRIx64,
                     ctx.pdev->name, ctx.bar,
                     ctx.guest_bar_start, host_bar_start);
        return -EOPNOTSUPP;
    }

    *host_pci_bus_addr = host_bar_start + ctx.offset;
    return 0;
}

static int spapr_vfio_errinjct_rtas_type_to_vfio(uint32_t rtas_type)
{
    switch (rtas_type) {
    case RTAS_ERR_TYPE_IOA_BUS_ERROR:
        return EEH_ERR_TYPE_32;
    case RTAS_ERR_TYPE_IOA_BUS_ERROR_64:
        return EEH_ERR_TYPE_64;
    default:
        return -1;
    }
}

static bool spapr_vfio_errinjct_func_valid(uint32_t func)
{
    return func <= EEH_ERR_FUNC_MAX;
}

int spapr_phb_vfio_errinjct(SpaprPhbState *sphb, uint32_t type,
                            uint32_t func, uint64_t addr, uint64_t mask)
{
    VFIOLegacyContainer *container;
    struct vfio_eeh_pe_op op = {
        .op   = VFIO_EEH_PE_INJECT_ERR,
        .argsz = sizeof(op),
    };
    int vfio_type;

    if (!sphb) {
        return RTAS_OUT_PARAM_ERROR;
    }

    if (!spapr_vfio_errinjct_func_valid(func)) {
        return RTAS_OUT_PARAM_ERROR;
    }

    vfio_type = spapr_vfio_errinjct_rtas_type_to_vfio(type);
    if (vfio_type < 0) {
        return RTAS_OUT_NOT_SUPPORTED;
    }

    container = vfio_eeh_as_container(&sphb->iommu_as);
    if (!container) {
        error_report("vfio/eeh errinjct: no VFIO EEH container for PHB");
        return RTAS_OUT_NOT_SUPPORTED;
    }

    op.err.type = vfio_type;
    op.err.func = func;
    op.err.addr = addr;
    op.err.mask = mask;

    if (ioctl(container->fd, VFIO_EEH_PE_OP, &op) < 0) {
        error_report("vfio/eeh errinjct: VFIO_EEH_PE_OP failed: %s",
                     strerror(errno));
        switch (errno) {
        case EINVAL:
            return RTAS_OUT_PARAM_ERROR;
        case ENOTTY:
        case EOPNOTSUPP:
            return RTAS_OUT_NOT_SUPPORTED;
        default:
            return RTAS_OUT_HW_ERROR;
        }
    }

    return RTAS_OUT_SUCCESS;
}

