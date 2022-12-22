/*
 * QEMU INTEL 82576EB GbE NIC emulation
 *
 * Software developer's manuals:
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82576eg-gbe-datasheet.pdf
 *
 * Authors:
 * Sriram Yagnaraman <sriram.yagnaraman@est.tech>
 *
 * Based on work done by:
 * Knut Omang.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "net/eth.h"
#include "net/net.h"
#include "net/tap.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "e1000_regs.h"

#include "e1000x_common.h"
#include "igb_core.h"
#include "igb.h"

#include "trace.h"
#include "qapi/error.h"
#include "qom/object.h"

static uint64_t
igbvf_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    IgbvfState *s = opaque;
    return igb_core_read(&s->core, addr, size);
}

static void
igbvf_mmio_write(void *opaque, hwaddr addr,
                   uint64_t val, unsigned size)
{
    IgbvfState *s = opaque;
    igb_core_write(&s->core, addr, val, size);
}

static const MemoryRegionOps mmio_ops = {
    .read = igbvf_mmio_read,
    .write = igbvf_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void igbvf_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    int ret;
    IgbvfState *s = IGBVF(pci_dev);
    int vfNumber = pcie_sriov_vf_number(pci_dev);
    assert(vfNumber < IGB_TOTAL_VFS);

    s->core.pf = &IGB(pcie_sriov_get_pf(pci_dev))->core;
    s->core.pf->vfs[vfNumber] = &s->core;

    memory_region_init_io(&s->mmio, OBJECT(s), &mmio_ops, s,
                          "igbvf-mmio", IGBVF_MMIO_SIZE);
    pcie_sriov_vf_register_bar(pci_dev, IGB_MMIO_IDX, &s->mmio);

    memory_region_init(&s->msix, OBJECT(s), "igbvf-msix", IGBVF_MSIX_SIZE);
    pcie_sriov_vf_register_bar(pci_dev, IGB_MSIX_IDX, &s->msix);
    ret = msix_init(pci_dev, IGB_MSIX_VECTORS_VF, &s->msix, IGB_MSIX_BAR,
                    0, &s->msix, IGB_MSIX_BAR, IGB_MSIX_PBA,
                    IGB_PCIE_MSIX_CAP_OFFSET, errp);
    if (ret) {
        goto err_msix;
    }

    for (int v = 0; v < IGB_MSIX_VECTORS_VF; v++) {
        msix_vector_use(pci_dev, v);
    }

    ret = pcie_endpoint_cap_init(pci_dev, IGB_PCIE_PCIE_CAP_OFFSET);
    if (ret < 0) {
        goto err_pcie_cap;
    }

    if (pcie_aer_init(pci_dev, PCI_ERR_VER, IGB_PCIE_AER_CAP_OFFSET,
                      PCI_ERR_SIZEOF, errp) < 0) {
        goto err_aer;
    }

    pcie_ari_init(pci_dev, IGB_PCIE_ARI_CAP_OFFSET, 1);

    /* Initialize core */
    s->core.owner = &s->parent_obj;
    s->core.owner_nic = s->core.pf->owner_nic;
    s->core.max_queue_num = s->core.pf->max_queue_num;
    s->core.has_vnet = s->core.pf->has_vnet;

    igb_core_pci_realize(&s->core, NULL, 0, NULL);
    return;

 err_aer:
    pcie_cap_exit(pci_dev);
 err_pcie_cap:
    msix_unuse_all_vectors(pci_dev);
    msix_uninit(pci_dev, &s->msix, &s->msix);
 err_msix:
    return;
}

static void igbvf_reset(DeviceState *dev)
{
    IgbvfState *igb = IGBVF(dev);

    trace_igb_cb_qdev_reset();

    igb_core_reset(&igb->core);
}

static void igbvf_pci_uninit(PCIDevice *pci_dev)
{
    IgbvfState *igb = IGBVF(pci_dev);
    MemoryRegion *mr = &igb->msix;

    igb_core_pci_uninit(&igb->core);

    pcie_aer_exit(pci_dev);
    pcie_cap_exit(pci_dev);

    msix_uninit(pci_dev, mr, mr);
}

static int igbvf_pre_save(void *opaque)
{
    IgbvfState *s = opaque;

    trace_igb_cb_pre_save();

    igb_core_pre_save(&s->core);

    return 0;
}

static int igbvf_post_load(void *opaque, int version_id)
{
    IgbvfState *s = opaque;

    trace_igb_cb_post_load();

    return igb_core_post_load(&s->core);
}

static const VMStateDescription igbvf_vmstate = {
    .name = TYPE_IGBVF,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = igbvf_pre_save,
    .post_load = igbvf_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, IgbvfState),
        VMSTATE_MSIX(parent_obj, IgbvfState),

        VMSTATE_UINT8(core.rx_desc_len, IgbvfState),
        VMSTATE_UINT32_ARRAY(core.rx_desc_buf_size, IgbvfState, IGB_NUM_QUEUES),
        VMSTATE_UINT32_ARRAY(core.rx_desc_buf_size, IgbvfState, IGB_NUM_QUEUES),

        VMSTATE_UINT16_ARRAY(core.eeprom, IgbvfState, IGB_EEPROM_SIZE),
        VMSTATE_UINT16_2DARRAY(core.phy, IgbvfState,
                               IGB_PHY_PAGES, IGB_PHY_PAGE_SIZE),
        VMSTATE_UINT32_ARRAY(core.mac, IgbvfState, IGB_MAC_SIZE),
        VMSTATE_UINT8_ARRAY(core.permanent_mac, IgbvfState, ETH_ALEN),

        VMSTATE_UINT32(core.delayed_causes, IgbvfState),

        VMSTATE_IGB_INTR_DELAY_TIMER_ARRAY(core.eitr, IgbvfState,
                                              IGB_MSIX_VEC_NUM),
        VMSTATE_BOOL_ARRAY(core.eitr_intr_pending, IgbvfState,
                           IGB_MSIX_VEC_NUM),

        VMSTATE_UINT32_ARRAY(core.eitr_guest_value, IgbvfState,
                             IGB_MSIX_VEC_NUM),

        VMSTATE_UINT16(core.vet, IgbvfState),

        VMSTATE_STRUCT_ARRAY(core.tx_ctx, IgbvfState, IGB_NUM_TX_CTX, 0,
                             igb_vmstate_tx_ctx, struct igb_tx_ctx),
        VMSTATE_STRUCT_ARRAY(core.tx, IgbvfState, IGB_NUM_QUEUES, 0,
                             igb_vmstate_tx, struct igb_tx),
        VMSTATE_END_OF_LIST()
    }
};

static void igbvf_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);

    c->realize = igbvf_pci_realize;
    c->exit = igbvf_pci_uninit;
    c->vendor_id = PCI_VENDOR_ID_INTEL;
    c->device_id = E1000_DEV_ID_82576_VF;
    c->revision = 1;
    c->romfile = NULL;
    c->class_id = PCI_CLASS_NETWORK_ETHERNET;

    dc->desc = "Intel 82576 GbE Controller Virtual Function";
    dc->reset = igbvf_reset;
    dc->vmsd = &igbvf_vmstate;
}

static void igbvf_instance_init(Object *obj)
{
}

static const TypeInfo igbvf_info = {
    .name = TYPE_IGBVF,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IgbvfState),
    .class_init = igbvf_class_init,
    .instance_init = igbvf_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void igbvf_register_types(void)
{
    type_register_static(&igbvf_info);
}

type_init(igbvf_register_types)
