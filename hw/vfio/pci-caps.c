/*
 * vfio PCI device - PCI capabilities setup
 *
 * Copyright Red Hat, Inc. 2012-2026
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>

#include "hw/core/iommu.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_regs.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "pci.h"
#include "trace.h"
#include "qapi/error.h"
#include "vfio-helpers.h"

static uint8_t vfio_std_cap_max_size(PCIDevice *pdev, uint8_t pos)
{
    uint8_t tmp;
    uint16_t next = PCI_CONFIG_SPACE_SIZE;

    for (tmp = pdev->config[PCI_CAPABILITY_LIST]; tmp;
         tmp = pdev->config[tmp + PCI_CAP_LIST_NEXT]) {
        if (tmp > pos && tmp < next) {
            next = tmp;
        }
    }

    return next - pos;
}


static uint16_t vfio_ext_cap_max_size(const uint8_t *config, uint16_t pos)
{
    uint16_t tmp, next = PCIE_CONFIG_SPACE_SIZE;

    for (tmp = PCI_CONFIG_SPACE_SIZE; tmp;
        tmp = PCI_EXT_CAP_NEXT(pci_get_long(config + tmp))) {
        if (tmp > pos && tmp < next) {
            next = tmp;
        }
    }

    return next - pos;
}

static void vfio_set_word_bits(uint8_t *buf, uint16_t val, uint16_t mask)
{
    pci_set_word(buf, (pci_get_word(buf) & ~mask) | val);
}

void vfio_pci_cap_add_emulated_word(VFIOPCIDevice *vdev, int pos,
                            uint16_t val, uint16_t mask)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    vfio_set_word_bits(pdev->config + pos, val, mask);
    vfio_set_word_bits(pdev->wmask + pos, ~mask, mask);
    vfio_set_word_bits(vdev->emulated_config_bits + pos, mask, mask);
}

static void vfio_set_long_bits(uint8_t *buf, uint32_t val, uint32_t mask)
{
    pci_set_long(buf, (pci_get_long(buf) & ~mask) | val);
}

void vfio_pci_cap_add_emulated_long(VFIOPCIDevice *vdev, int pos,
                            uint32_t val, uint32_t mask)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    vfio_set_long_bits(pdev->config + pos, val, mask);
    vfio_set_long_bits(pdev->wmask + pos, ~mask, mask);
    vfio_set_long_bits(vdev->emulated_config_bits + pos, mask, mask);
}

static void vfio_pci_enable_rp_atomics(VFIOPCIDevice *vdev)
{
    struct vfio_device_info_cap_pci_atomic_comp *cap;
    g_autofree struct vfio_device_info *info = NULL;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    PCIBus *bus = pci_get_bus(pdev);
    PCIDevice *parent = bus->parent_dev;
    struct vfio_info_cap_header *hdr;
    uint32_t mask = 0;
    uint8_t *pos;

    /*
     * PCIe Atomic Ops completer support is only added automatically for single
     * function devices downstream of a root port supporting DEVCAP2.  Support
     * is added during realize and, if added, removed during device exit.  The
     * single function requirement avoids conflicting requirements should a
     * slot be composed of multiple devices with differing capabilities.
     */
    if (pci_bus_is_root(bus) || !parent || !parent->exp.exp_cap ||
        pcie_cap_get_type(parent) != PCI_EXP_TYPE_ROOT_PORT ||
        pcie_cap_get_version(parent) != PCI_EXP_FLAGS_VER2 ||
        pdev->devfn ||
        pdev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        return;
    }

    pos = parent->config + parent->exp.exp_cap + PCI_EXP_DEVCAP2;

    /* Abort if there'a already an Atomic Ops configuration on the root port */
    if (pci_get_long(pos) & (PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
                             PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
                             PCI_EXP_DEVCAP2_ATOMIC_COMP128)) {
        return;
    }

    info = vfio_get_device_info(vdev->vbasedev.fd);
    if (!info) {
        return;
    }

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_PCI_ATOMIC_COMP);
    if (!hdr) {
        return;
    }

    cap = (void *)hdr;
    if (cap->flags & VFIO_PCI_ATOMIC_COMP32) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP32;
    }
    if (cap->flags & VFIO_PCI_ATOMIC_COMP64) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP64;
    }
    if (cap->flags & VFIO_PCI_ATOMIC_COMP128) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP128;
    }

    if (!mask) {
        return;
    }

    pci_long_test_and_set_mask(pos, mask);
    vdev->clear_parent_atomics_on_exit = true;
}

void vfio_pci_cap_disable_rp_atomics(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if (vdev->clear_parent_atomics_on_exit) {
        PCIDevice *parent = pci_get_bus(pdev)->parent_dev;
        uint8_t *pos = parent->config + parent->exp.exp_cap + PCI_EXP_DEVCAP2;

        pci_long_test_and_clear_mask(pos, PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
                                          PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
                                          PCI_EXP_DEVCAP2_ATOMIC_COMP128);
    }
}

static bool vfio_setup_pcie_cap(VFIOPCIDevice *vdev, int pos, uint8_t size,
                                Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t flags;
    uint8_t type;

    flags = pci_get_word(pdev->config + pos + PCI_CAP_FLAGS);
    type = (flags & PCI_EXP_FLAGS_TYPE) >> 4;

    if (type != PCI_EXP_TYPE_ENDPOINT &&
        type != PCI_EXP_TYPE_LEG_END &&
        type != PCI_EXP_TYPE_RC_END) {

        error_setg(errp, "assignment of PCIe type 0x%x "
                   "devices is not currently supported", type);
        return false;
    }

    if (!pci_bus_is_express(pci_get_bus(pdev))) {
        PCIBus *bus = pci_get_bus(pdev);
        PCIDevice *bridge;

        /*
         * Traditionally PCI device assignment exposes the PCIe capability
         * as-is on non-express buses.  The reason being that some drivers
         * simply assume that it's there, for example tg3.  However when
         * we're running on a native PCIe machine type, like Q35, we need
         * to hide the PCIe capability.  The reason for this is twofold;
         * first Windows guests get a Code 10 error when the PCIe capability
         * is exposed in this configuration.  Therefore express devices won't
         * work at all unless they're attached to express buses in the VM.
         * Second, a native PCIe machine introduces the possibility of fine
         * granularity IOMMUs supporting both translation and isolation.
         * Guest code to discover the IOMMU visibility of a device, such as
         * IOMMU grouping code on Linux, is very aware of device types and
         * valid transitions between bus types.  An express device on a non-
         * express bus is not a valid combination on bare metal systems.
         *
         * Drivers that require a PCIe capability to make the device
         * functional are simply going to need to have their devices placed
         * on a PCIe bus in the VM.
         */
        while (!pci_bus_is_root(bus)) {
            bridge = pci_bridge_get_device(bus);
            bus = pci_get_bus(bridge);
        }

        if (pci_bus_is_express(bus)) {
            return true;
        }

    } else if (pci_bus_is_root(pci_get_bus(pdev))) {
        /*
         * On a Root Complex bus Endpoints become Root Complex Integrated
         * Endpoints, which changes the type and clears the LNK & LNK2 fields.
         */
        if (type == PCI_EXP_TYPE_ENDPOINT) {
            vfio_pci_cap_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                                   PCI_EXP_TYPE_RC_END << 4,
                                   PCI_EXP_FLAGS_TYPE);

            /* Link Capabilities, Status, and Control goes away */
            if (size > PCI_EXP_LNKCTL) {
                vfio_pci_cap_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP, 0, ~0);
                vfio_pci_cap_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL, 0, ~0);
                vfio_pci_cap_add_emulated_word(vdev, pos + PCI_EXP_LNKSTA, 0, ~0);

#ifndef PCI_EXP_LNKCAP2
#define PCI_EXP_LNKCAP2 44
#endif
#ifndef PCI_EXP_LNKSTA2
#define PCI_EXP_LNKSTA2 50
#endif
                /* Link 2 Capabilities, Status, and Control goes away */
                if (size > PCI_EXP_LNKCAP2) {
                    vfio_pci_cap_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP2, 0, ~0);
                    vfio_pci_cap_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL2, 0, ~0);
                    vfio_pci_cap_add_emulated_word(vdev, pos + PCI_EXP_LNKSTA2, 0, ~0);
                }
            }

        } else if (type == PCI_EXP_TYPE_LEG_END) {
            /*
             * Legacy endpoints don't belong on the root complex.  Windows
             * seems to be happier with devices if we skip the capability.
             */
            return true;
        }

    } else {
        /*
         * Convert Root Complex Integrated Endpoints to regular endpoints.
         * These devices don't support LNK/LNK2 capabilities, so make them up.
         */
        if (type == PCI_EXP_TYPE_RC_END) {
            vfio_pci_cap_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                                   PCI_EXP_TYPE_ENDPOINT << 4,
                                   PCI_EXP_FLAGS_TYPE);
            vfio_pci_cap_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP,
                           QEMU_PCI_EXP_LNKCAP_MLW(QEMU_PCI_EXP_LNK_X1) |
                           QEMU_PCI_EXP_LNKCAP_MLS(QEMU_PCI_EXP_LNK_2_5GT), ~0);
            vfio_pci_cap_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL, 0, ~0);
        }

        vfio_pci_enable_rp_atomics(vdev);
    }

    /*
     * Intel 82599 SR-IOV VFs report an invalid PCIe capability version 0
     * (Niantic errate #35) causing Windows to error with a Code 10 for the
     * device on Q35.  Fixup any such devices to report version 1.  If we
     * were to remove the capability entirely the guest would lose extended
     * config space.
     */
    if ((flags & PCI_EXP_FLAGS_VERS) == 0) {
        vfio_pci_cap_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                               1, PCI_EXP_FLAGS_VERS);
    }

    pos = pci_add_capability(pdev, PCI_CAP_ID_EXP, pos, size, errp);
    if (pos < 0) {
        return false;
    }

    pdev->exp.exp_cap = pos;

    return true;
}

static void vfio_check_pcie_flr(VFIOPCIDevice *vdev, uint8_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint32_t cap = pci_get_long(pdev->config + pos + PCI_EXP_DEVCAP);

    if (cap & PCI_EXP_DEVCAP_FLR) {
        trace_vfio_check_pcie_flr(vdev->vbasedev.name);
        vdev->has_flr = true;
    }
}

static void vfio_check_pm_reset(VFIOPCIDevice *vdev, uint8_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t csr = pci_get_word(pdev->config + pos + PCI_PM_CTRL);

    if (!(csr & PCI_PM_CTRL_NO_SOFT_RESET)) {
        trace_vfio_check_pm_reset(vdev->vbasedev.name);
        vdev->has_pm_reset = true;
    }
}

static void vfio_check_af_flr(VFIOPCIDevice *vdev, uint8_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint8_t cap = pci_get_byte(pdev->config + pos + PCI_AF_CAP);

    if ((cap & PCI_AF_CAP_TP) && (cap & PCI_AF_CAP_FLR)) {
        trace_vfio_check_af_flr(vdev->vbasedev.name);
        vdev->has_flr = true;
    }
}

static bool vfio_add_vendor_specific_cap(VFIOPCIDevice *vdev, int pos,
                                         uint8_t size, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, pos, size, errp);
    if (pos < 0) {
        return false;
    }

    /*
     * Exempt config space check for Vendor Specific Information during
     * restore/load.
     * Config space check is still enforced for 3 byte VSC header.
     */
    if (vdev->skip_vsc_check && size > 3) {
        memset(pdev->cmask + pos + 3, 0, size - 3);
    }

    return true;
}

static bool vfio_add_std_cap(VFIOPCIDevice *vdev, uint8_t pos, Error **errp)
{
    ERRP_GUARD();
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint8_t cap_id, next, size;
    bool ret;

    cap_id = pdev->config[pos];
    next = pdev->config[pos + PCI_CAP_LIST_NEXT];

    /*
     * If it becomes important to configure capabilities to their actual
     * size, use this as the default when it's something we don't recognize.
     * Since QEMU doesn't actually handle many of the config accesses,
     * exact size doesn't seem worthwhile.
     */
    size = vfio_std_cap_max_size(pdev, pos);

    /*
     * pci_add_capability always inserts the new capability at the head
     * of the chain.  Therefore to end up with a chain that matches the
     * physical device, we insert from the end by making this recursive.
     * This is also why we pre-calculate size above as cached config space
     * will be changed as we unwind the stack.
     */
    if (next) {
        if (!vfio_add_std_cap(vdev, next, errp)) {
            return false;
        }
    } else {
        /* Begin the rebuild, use QEMU emulated list bits */
        pdev->config[PCI_CAPABILITY_LIST] = 0;
        vdev->emulated_config_bits[PCI_CAPABILITY_LIST] = 0xff;
        vdev->emulated_config_bits[PCI_STATUS] |= PCI_STATUS_CAP_LIST;

        if (!vfio_add_virt_caps(vdev, errp)) {
            return false;
        }
    }

    /* Scale down size, esp in case virt caps were added above */
    size = MIN(size, vfio_std_cap_max_size(pdev, pos));

    /* Use emulated next pointer to allow dropping caps */
    pci_set_byte(vdev->emulated_config_bits + pos + PCI_CAP_LIST_NEXT, 0xff);

    switch (cap_id) {
    case PCI_CAP_ID_MSI:
        ret = vfio_pci_msi_setup(vdev, pos, errp);
        break;
    case PCI_CAP_ID_EXP:
        vfio_check_pcie_flr(vdev, pos);
        ret = vfio_setup_pcie_cap(vdev, pos, size, errp);
        break;
    case PCI_CAP_ID_MSIX:
        ret = vfio_pci_msix_setup(vdev, pos, errp);
        break;
    case PCI_CAP_ID_PM:
        vfio_check_pm_reset(vdev, pos);
        ret = pci_pm_init(pdev, pos, errp) >= 0;
        /*
         * PCI-core config space emulation needs write access to the power
         * state enabled for tracking BAR mapping relative to PM state.
         */
        pci_set_word(pdev->wmask + pos + PCI_PM_CTRL, PCI_PM_CTRL_STATE_MASK);
        break;
    case PCI_CAP_ID_AF:
        vfio_check_af_flr(vdev, pos);
        ret = pci_add_capability(pdev, cap_id, pos, size, errp) >= 0;
        break;
    case PCI_CAP_ID_VNDR:
        ret = vfio_add_vendor_specific_cap(vdev, pos, size, errp);
        break;
    default:
        ret = pci_add_capability(pdev, cap_id, pos, size, errp) >= 0;
        break;
    }

    if (!ret) {
        error_prepend(errp,
                      "failed to add PCI capability 0x%x[0x%x]@0x%x: ",
                      cap_id, size, pos);
    }

    return ret;
}

static int vfio_setup_rebar_ecap(VFIOPCIDevice *vdev, uint16_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint32_t ctrl;
    int i, nbar;

    ctrl = pci_get_long(pdev->config + pos + PCI_REBAR_CTRL);
    nbar = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >> PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbar; i++) {
        uint32_t cap;
        int size;

        ctrl = pci_get_long(pdev->config + pos + PCI_REBAR_CTRL + (i * 8));
        size = (ctrl & PCI_REBAR_CTRL_BAR_SIZE) >> PCI_REBAR_CTRL_BAR_SHIFT;

        /* The cap register reports sizes 1MB to 128TB, with 4 reserved bits */
        cap = size <= 27 ? 1U << (size + 4) : 0;

        /*
         * The PCIe spec (v6.0.1, 7.8.6) requires HW to support at least one
         * size in the range 1MB to 512GB.  We intend to mask all sizes except
         * the one currently enabled in the size field, therefore if it's
         * outside the range, hide the whole capability as this virtualization
         * trick won't work.  If >512GB resizable BARs start to appear, we
         * might need an opt-in or reservation scheme in the kernel.
         */
        if (!(cap & PCI_REBAR_CAP_SIZES)) {
            return -EINVAL;
        }

        /* Hide all sizes reported in the ctrl reg per above requirement. */
        ctrl &= (PCI_REBAR_CTRL_BAR_SIZE |
                 PCI_REBAR_CTRL_NBAR_MASK |
                 PCI_REBAR_CTRL_BAR_IDX);

        /*
         * The BAR size field is RW, however we've mangled the capability
         * register such that we only report a single size, ie. the current
         * BAR size.  A write of an unsupported value is undefined, therefore
         * the register field is essentially RO.
         */
        vfio_pci_cap_add_emulated_long(vdev, pos + PCI_REBAR_CAP + (i * 8), cap, ~0);
        vfio_pci_cap_add_emulated_long(vdev, pos + PCI_REBAR_CTRL + (i * 8), ctrl, ~0);
    }

    return 0;
}

/*
 * Try to retrieve PASID capability information via IOMMUFD APIs and,
 * if supported, synthesize a PASID PCIe extended capability for the
 * VFIO device.
 *
 * Use user-specified PASID capability offset if provided, otherwise
 * place it at the end of the PCIe extended configuration space.
 */
static bool vfio_pci_synthesize_pasid_cap(VFIOPCIDevice *vdev, Error **errp)
{
    HostIOMMUDevice *hiod = vdev->vbasedev.hiod;
    HostIOMMUDeviceClass *hiodc;
    PasidInfo pasid_info;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t pasid_offset;

    if (!hiod) {
        return true;
    }

    hiodc = HOST_IOMMU_DEVICE_GET_CLASS(hiod);
    if (!hiodc || !hiodc->get_pasid_info ||
        !hiodc->get_pasid_info(hiod, &pasid_info) ||
        !(pci_device_get_viommu_flags(pdev) & VIOMMU_FLAG_PASID_SUPPORTED)) {
        return true;
    }

    /* Use user-specified offset if set, otherwise place PASID at the end. */
    if (vdev->vpasid_cap_offset) {
        pasid_offset = vdev->vpasid_cap_offset;
    } else {
        pasid_offset = PCIE_CONFIG_SPACE_SIZE - PCI_EXT_CAP_PASID_SIZEOF;
    }

    if (!pcie_insert_capability(pdev, PCI_EXT_CAP_ID_PASID, PCI_PASID_VER,
                                pasid_offset, PCI_EXT_CAP_PASID_SIZEOF)) {
        error_setg(errp, "vfio: Placing PASID capability at offset 0x%x failed",
                   pasid_offset);
        return false;
    }
    trace_vfio_pci_synthesize_pasid_cap(vdev->vbasedev.name, pasid_offset);

    pcie_pasid_common_init(pdev, pasid_offset, pasid_info.max_pasid_log2,
                           pasid_info.exec_perm, pasid_info.priv_mod);

    /* PASID capability is fully emulated by QEMU */
    memset(vdev->emulated_config_bits + pdev->exp.pasid_cap, 0xff,
           PCI_EXT_CAP_PASID_SIZEOF);
    return true;
}

/*
 * Determine whether ATS capability should be advertised for @vdev, based on
 * whether it was enabled on the command line and whether it is supported
 * according to the kernel.
 *
 * Store whether ATS capability should be advertised in @ats_needed.
 *
 * Returns false only when ats=on is explicitly requested but the kernel
 * reports it is not supported. Returns true in all other cases.
 */
static bool vfio_pci_ats_requested_and_supported(VFIOPCIDevice *vdev,
                                                 bool *ats_needed, Error **errp)
{
    HostIOMMUDevice *hiod = vdev->vbasedev.hiod;
    HostIOMMUDeviceClass *hiodc;
    bool ats_supported;
    *ats_needed = false;

    if (vdev->ats == ON_OFF_AUTO_OFF) {
        return true;
    }

    *ats_needed = true;
    if (!hiod) {
        return true;
    }
    hiodc = HOST_IOMMU_DEVICE_GET_CLASS(hiod);
    if (!hiodc || !hiodc->support_ats) {
        return true;
    }

    ats_supported = hiodc->support_ats(hiod);
    if (vdev->ats == ON_OFF_AUTO_ON && !ats_supported) {
        error_setg(errp, "vfio-pci: ATS requested but not supported by kernel");
        *ats_needed = false;
        return false;
    }

    *ats_needed = ats_supported;
    return true;
}

static void vfio_add_ext_cap(VFIOPCIDevice *vdev, bool ats_needed)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    bool pasid_cap_added = false;
    bool ats_cap_present = false;
    Error *err = NULL;
    uint32_t header;
    uint16_t cap_id, next, size;
    uint8_t cap_ver;
    uint8_t *config;

    /* Only add extended caps if we have them and the guest can see them */
    if (!pci_is_express(pdev) || !pci_bus_is_express(pci_get_bus(pdev)) ||
        !pci_get_long(pdev->config + PCI_CONFIG_SPACE_SIZE)) {
        return;
    }

    /*
     * pcie_add_capability always inserts the new capability at the tail
     * of the chain.  Therefore to end up with a chain that matches the
     * physical device, we cache the config space to avoid overwriting
     * the original config space when we parse the extended capabilities.
     */
    config = g_memdup2(pdev->config, vdev->config_size);

    /*
     * Extended capabilities are chained with each pointing to the next, so we
     * can drop anything other than the head of the chain simply by modifying
     * the previous next pointer.  Seed the head of the chain here such that
     * we can simply skip any capabilities we want to drop below, regardless
     * of their position in the chain.  If this stub capability still exists
     * after we add the capabilities we want to expose, update the capability
     * ID to zero.  Note that we cannot seed with the capability header being
     * zero as this conflicts with definition of an absent capability chain
     * and prevents capabilities beyond the head of the list from being added.
     * By replacing the dummy capability ID with zero after walking the device
     * chain, we also transparently mark extended capabilities as absent if
     * no capabilities were added.  Note that the PCIe spec defines an absence
     * of extended capabilities to be determined by a value of zero for the
     * capability ID, version, AND next pointer.  A non-zero next pointer
     * should be sufficient to indicate additional capabilities are present,
     * which will occur if we call pcie_add_capability() below.  The entire
     * first dword is emulated to support this.
     *
     * NB. The kernel side does similar masking, so be prepared that our
     * view of the device may also contain a capability ID zero in the head
     * of the chain.  Skip it for the same reason that we cannot seed the
     * chain with a zero capability.
     */
    pci_set_long(pdev->config + PCI_CONFIG_SPACE_SIZE,
                 PCI_EXT_CAP(0xFFFF, 0, 0));
    pci_set_long(pdev->wmask + PCI_CONFIG_SPACE_SIZE, 0);
    pci_set_long(vdev->emulated_config_bits + PCI_CONFIG_SPACE_SIZE, ~0);

    for (next = PCI_CONFIG_SPACE_SIZE; next;
         next = PCI_EXT_CAP_NEXT(pci_get_long(config + next))) {
        header = pci_get_long(config + next);
        cap_id = PCI_EXT_CAP_ID(header);
        cap_ver = PCI_EXT_CAP_VER(header);

        /*
         * If it becomes important to configure extended capabilities to their
         * actual size, use this as the default when it's something we don't
         * recognize. Since QEMU doesn't actually handle many of the config
         * accesses, exact size doesn't seem worthwhile.
         */
        size = vfio_ext_cap_max_size(config, next);

        /* Use emulated next pointer to allow dropping extended caps */
        pci_long_test_and_set_mask(vdev->emulated_config_bits + next,
                                   PCI_EXT_CAP_NEXT_MASK);

        switch (cap_id) {
        case 0: /* kernel masked capability */
        case PCI_EXT_CAP_ID_SRIOV: /* Read-only VF BARs confuse OVMF */
        case PCI_EXT_CAP_ID_ARI: /* XXX Needs next function virtualization */
            trace_vfio_add_ext_cap_dropped(vdev->vbasedev.name, cap_id, next);
            break;
        case PCI_EXT_CAP_ID_REBAR:
            if (!vfio_setup_rebar_ecap(vdev, next)) {
                pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            }
            break;
        /*
         * VFIO kernel does not expose the PASID CAP today. We may synthesize
         * one later through IOMMUFD APIs. If VFIO ever starts exposing it,
         * record its presence here so we do not create a duplicate CAP.
         */
        case PCI_EXT_CAP_ID_PASID:
            pasid_cap_added = true;
            pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            break;
        case PCI_EXT_CAP_ID_ATS:
            ats_cap_present = true;
            /*
             * If ATS is requested and supported according to the kernel, add
             * the ATS capability. If not supported according to the kernel or
             * disabled on the qemu command line, omit the ATS cap.
             */
            if (ats_needed) {
                pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            }
            break;
        default:
            pcie_add_capability(pdev, cap_id, cap_ver, next, size);
        }

    }

    if (!pasid_cap_added && !vfio_pci_synthesize_pasid_cap(vdev, &err)) {
        error_report_err(err);
    }

    if (vdev->ats == ON_OFF_AUTO_ON && !ats_cap_present) {
        warn_report("vfio-pci: ats=on requested, but host device has no "
                    "ATS extended capability");
    }

    if (vdev->ats == ON_OFF_AUTO_AUTO && ats_cap_present && !ats_needed) {
        warn_report("vfio-pci: host kernel reports ATS unsupported; "
                    "ATS capability will be masked");
    }

    /* Cleanup chain head ID if necessary */
    if (pci_get_word(pdev->config + PCI_CONFIG_SPACE_SIZE) == 0xFFFF) {
        pci_set_word(pdev->config + PCI_CONFIG_SPACE_SIZE, 0);
    }

    g_free(config);
}

bool vfio_pci_add_capabilities(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    bool ats_needed = false;

    if (!(pdev->config[PCI_STATUS] & PCI_STATUS_CAP_LIST) ||
        !pdev->config[PCI_CAPABILITY_LIST]) {
        return true; /* Nothing to add */
    }

    if (!vfio_add_std_cap(vdev, pdev->config[PCI_CAPABILITY_LIST], errp)) {
        return false;
    }

    if (!vfio_pci_ats_requested_and_supported(vdev, &ats_needed, errp)) {
        return false;
    }

    vfio_add_ext_cap(vdev, ats_needed);
    return true;
}
