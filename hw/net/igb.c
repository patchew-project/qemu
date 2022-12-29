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
igb_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    IgbState *s = opaque;
    return igb_core_read(&s->core, addr, size);
}

static void
igb_mmio_write(void *opaque, hwaddr addr,
                   uint64_t val, unsigned size)
{
    IgbState *s = opaque;
    igb_core_write(&s->core, addr, val, size);
}

static bool
igb_io_get_reg_index(IgbState *s, uint32_t *idx)
{
    if (s->ioaddr < 0x1FFFF) {
        *idx = s->ioaddr;
        return true;
    }

    if (s->ioaddr < 0x7FFFF) {
        trace_igb_wrn_io_addr_undefined(s->ioaddr);
        return false;
    }

    if (s->ioaddr < 0xFFFFF) {
        trace_igb_wrn_io_addr_flash(s->ioaddr);
        return false;
    }

    trace_igb_wrn_io_addr_unknown(s->ioaddr);
    return false;
}

static uint64_t
igb_io_read(void *opaque, hwaddr addr, unsigned size)
{
    IgbState *s = opaque;
    uint32_t idx = 0;
    uint64_t val;

    switch (addr) {
    case E1000_IOADDR:
        trace_igb_io_read_addr(s->ioaddr);
        return s->ioaddr;
    case E1000_IODATA:
        if (igb_io_get_reg_index(s, &idx)) {
            val = igb_core_read(&s->core, idx, sizeof(val));
            trace_igb_io_read_data(idx, val);
            return val;
        }
        return 0;
    default:
        trace_igb_wrn_io_read_unknown(addr);
        return 0;
    }
}

static void
igb_io_write(void *opaque, hwaddr addr,
                uint64_t val, unsigned size)
{
    IgbState *s = opaque;
    uint32_t idx = 0;

    switch (addr) {
    case E1000_IOADDR:
        trace_igb_io_write_addr(val);
        s->ioaddr = (uint32_t) val;
        return;
    case E1000_IODATA:
        if (igb_io_get_reg_index(s, &idx)) {
            trace_igb_io_write_data(idx, val);
            igb_core_write(&s->core, idx, val, sizeof(val));
        }
        return;
    default:
        trace_igb_wrn_io_write_unknown(addr);
        return;
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = igb_mmio_read,
    .write = igb_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps io_ops = {
    .read = igb_io_read,
    .write = igb_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool
igb_nc_can_receive(NetClientState *nc)
{
    IgbState *s = qemu_get_nic_opaque(nc);
    return igb_can_receive(&s->core);
}

static ssize_t
igb_nc_receive_iov(NetClientState *nc, const struct iovec *iov, int iovcnt)
{
    IgbState *s = qemu_get_nic_opaque(nc);
    return igb_receive_iov(&s->core, iov, iovcnt);
}

static ssize_t
igb_nc_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    IgbState *s = qemu_get_nic_opaque(nc);
    return igb_receive(&s->core, buf, size);
}

static void
igb_set_link_status(NetClientState *nc)
{
    IgbState *s = qemu_get_nic_opaque(nc);
    igb_core_set_link_status(&s->core);
}

static NetClientInfo net_igb_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = igb_nc_can_receive,
    .receive = igb_nc_receive,
    .receive_iov = igb_nc_receive_iov,
    .link_status_changed = igb_set_link_status,
};

static void
igb_unuse_msix_vectors(IgbState *s, int num_vectors)
{
    int i;
    for (i = 0; i < num_vectors; i++) {
        msix_vector_unuse(PCI_DEVICE(s), i);
    }
}

static void
igb_use_msix_vectors(IgbState *s, int num_vectors)
{
    for (int i = 0; i < num_vectors; i++) {
        msix_vector_use(PCI_DEVICE(s), i);
    }
}

static void
igb_init_msix(IgbState *s)
{
    int res = msix_init(PCI_DEVICE(s), IGB_MSIX_VECTORS_PF,
                        &s->msix,
                        IGB_MSIX_IDX, IGB_MSIX_TABLE,
                        &s->msix,
                        IGB_MSIX_IDX, IGB_MSIX_PBA,
                        IGB_PCIE_MSIX_CAP_OFFSET, NULL);

    if (res < 0) {
        trace_igb_msix_init_fail(res);
    } else {
        igb_use_msix_vectors(s, IGB_MSIX_VECTORS_PF);
    }
}

static void
igb_cleanup_msix(IgbState *s)
{
    if (msix_present(PCI_DEVICE(s))) {
        igb_unuse_msix_vectors(s, IGB_MSIX_VECTORS_PF);
        msix_uninit(PCI_DEVICE(s), &s->msix, &s->msix);
    }
}

static void
igb_init_net_peer(IgbState *s, PCIDevice *pci_dev, uint8_t *macaddr)
{
    DeviceState *dev = DEVICE(pci_dev);
    NetClientState *peer;
    int i;

    s->nic = qemu_new_nic(&net_igb_info, &s->conf,
        object_get_typename(OBJECT(s)), dev->id, s);

    s->core.max_queue_num = s->conf.peers.queues ? s->conf.peers.queues - 1 : 0;

    trace_igb_mac_set_permanent(MAC_ARG(macaddr));
    memcpy(s->core.permanent_mac, macaddr, sizeof(s->core.permanent_mac));

    qemu_format_nic_info_str(qemu_get_queue(s->nic), macaddr);

    /* Assume support for vnet_hdr to begin with */
    s->core.has_vnet = true;

    for (i = 0; i < s->conf.peers.queues; i++) {
        peer = qemu_get_peer(s->nic->ncs, i);
        if (!qemu_has_vnet_hdr(peer)) {
            s->core.has_vnet = false;
        }
    }
    trace_igb_cfg_support_virtio(s->core.has_vnet);

    if (s->core.has_vnet) {
        for (i = 0; i < s->conf.peers.queues; i++) {
            peer = qemu_get_peer(s->nic->ncs, i);
            qemu_set_vnet_hdr_len(peer, sizeof(struct virtio_net_hdr));
            qemu_using_vnet_hdr(peer, true);
        }
    }
}

static inline uint64_t
igb_gen_dsn(uint8_t *mac)
{
    return (uint64_t)(mac[5])        |
           (uint64_t)(mac[4])  << 8  |
           (uint64_t)(mac[3])  << 16 |
           (uint64_t)(0x00FF)  << 24 |
           (uint64_t)(0x00FF)  << 32 |
           (uint64_t)(mac[2])  << 40 |
           (uint64_t)(mac[1])  << 48 |
           (uint64_t)(mac[0])  << 56;
}

static int
igb_add_pm_capability(PCIDevice *pdev, uint8_t offset, uint16_t pmc)
{
    Error *local_err = NULL;
    int ret = pci_add_capability(pdev, PCI_CAP_ID_PM, offset,
                                 PCI_PM_SIZEOF, &local_err);

    if (local_err) {
        error_report_err(local_err);
        return ret;
    }

    pci_set_word(pdev->config + offset + PCI_PM_PMC,
                 PCI_PM_CAP_VER_1_1 |
                 pmc);

    pci_set_word(pdev->wmask + offset + PCI_PM_CTRL,
                 PCI_PM_CTRL_STATE_MASK |
                 PCI_PM_CTRL_PME_ENABLE |
                 PCI_PM_CTRL_DATA_SEL_MASK);

    pci_set_word(pdev->w1cmask + offset + PCI_PM_CTRL,
                 PCI_PM_CTRL_PME_STATUS);

    return ret;
}

static void igb_write_config(PCIDevice *pci_dev, uint32_t address,
                                uint32_t val, int len)
{
    IgbState *s = IGB(pci_dev);

    pci_default_write_config(pci_dev, address, val, len);
    pcie_cap_flr_write_config(pci_dev, address, val, len);

    if (range_covers_byte(address, len, PCI_COMMAND) &&
        (pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        igb_start_recv(&s->core);
    }
}

/*
 * EEPROM (NVM) contents documented in section 6.1, table 6-1:
 * and in 6.10 Software accessed words.
 *
 * TBD: Need to walk through this, names in comments are ok up to 0x4F
 */
static const uint16_t igb_eeprom_template[80] = {
  /*        Address        |    Compat.    | ImRev |Compat.|OEM sp.*/
    0x0000, 0x0000, 0x0000, 0x0d14, 0xffff, 0x2010, 0xffff, 0xffff,
  /*      PBA      |ICtrl1 | SSID  | SVID  | DevID |-------|ICtrl2 */
    0x1040, 0xffff, 0x046b, 0x484c, 0x108e, 0x10c9, 0x0000, 0xf14b,
  /* SwPin0| DevID | EESZ  |-------|ICtrl3 |PCI-tc | MSIX  | APtr  */
    0xe30c, 0x10c9, 0x6000, 0x0000, 0x8c01, 0x0014, 0x4a40, 0x0060,
  /* PCIe Init. Conf 1,2,3 |PCICtrl| LD1,3 |DDevID |DevRev | LD0,2 */
    0x6cf6, 0xd7b0, 0x0a7e, 0x8403, 0x4784, 0x10a6, 0x0001, 0x4602,
  /* SwPin1| FunC  |LAN-PWR|ManHwC |ICtrl3 | IOVct |VDevID |-------*/
    0xe30c, 0x2620, 0x1ae5, 0x004a, 0x8401, 0x00f7, 0x10ca, 0x0000,
  /*---------------| LD1,3 | LD0,2 | ROEnd | ROSta | Wdog  | VPD   */
    0x0000, 0x0000, 0x4784, 0x4602, 0x0000, 0x0000, 0x0000, 0xffff,
  /* PCSet0| Ccfg0 |PXEver |IBAcap |PCSet1 | Ccfg1 |iSCVer | ??    */
    0x0100, 0x4000, 0x131f, 0x4013, 0x0100, 0x4000, 0xffff, 0xffff,
  /* PCSet2| Ccfg2 |PCSet3 | Ccfg3 | ??    |AltMacP| ??    |CHKSUM */
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x00e0, 0xffff, 0xb73b,
  /* ArbEn |-------| ImuID | ImuID |-------------------------------*/
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  /*----------------------- Reserved ------------------------------*/
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  /* Word 0x50 - 0x5XX (sec.6.5) */
};

static void igb_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    IgbState *s = IGB(pci_dev);
    uint8_t *macaddr;
    int ret;

    trace_igb_cb_pci_realize();

    pci_dev->config_write = igb_write_config;

    pci_dev->config[PCI_CACHE_LINE_SIZE] = 0x10;
    pci_dev->config[PCI_INTERRUPT_PIN] = 1;

    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID, s->subsys_ven);
    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID, s->subsys);

    s->subsys_ven_used = s->subsys_ven;
    s->subsys_used = s->subsys;

    /* Define IO/MMIO regions */
    memory_region_init_io(&s->mmio, OBJECT(s), &mmio_ops, s,
                          "igb-mmio", IGB_MMIO_SIZE);
    pci_register_bar(pci_dev, IGB_MMIO_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    memory_region_init(&s->flash, OBJECT(s),
                          "igb-flash", IGB_FLASH_SIZE);
    pci_register_bar(pci_dev, IGB_FLASH_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->flash);

    memory_region_init_io(&s->io, OBJECT(s), &io_ops, s,
                          "igb-io", IGB_IO_SIZE);
    pci_register_bar(pci_dev, IGB_IO_IDX,
                     PCI_BASE_ADDRESS_SPACE_IO, &s->io);

    memory_region_init(&s->msix, OBJECT(s), "igb-msix",
                       IGB_MSIX_SIZE);
    pci_register_bar(pci_dev, IGB_MSIX_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix);

    /* Create networking backend */
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    macaddr = s->conf.macaddr.a;

    igb_init_msix(s);

    if (pcie_endpoint_cap_init(pci_dev, IGB_PCIE_PCIE_CAP_OFFSET) < 0) {
        hw_error("Failed to initialize PCIe capability");
    }
    pcie_cap_flr_init(pci_dev);

    ret = msi_init(PCI_DEVICE(s), IGB_PCIE_MSI_CAP_OFFSET, 1, true, false, NULL);
    if (ret) {
        trace_igb_msi_init_fail(ret);
    }

    if (igb_add_pm_capability(pci_dev, IGB_PCIE_PM_CAP_OFFSET,
                                  PCI_PM_CAP_DSI) < 0) {
        hw_error("Failed to initialize PM capability");
    }

    if (pcie_aer_init(pci_dev, PCI_ERR_VER, IGB_PCIE_AER_CAP_OFFSET,
                      PCI_ERR_SIZEOF, NULL) < 0) {
        hw_error("Failed to initialize AER capability");
    }

    pcie_ari_init(pci_dev, IGB_PCIE_ARI_CAP_OFFSET, 1);

    pcie_sriov_pf_init(pci_dev, IGB_PCIE_SRIOV_CAP_OFFSET, TYPE_IGBVF,
        E1000_DEV_ID_82576_VF, IGB_TOTAL_VFS, IGB_TOTAL_VFS,
        IGB_VF_OFFSET, IGB_VF_STRIDE);

    pcie_sriov_pf_init_vf_bar(pci_dev, IGB_MMIO_IDX,
        PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH,
        IGBVF_MMIO_SIZE);
    pcie_sriov_pf_init_vf_bar(pci_dev, IGB_MSIX_IDX,
        PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH,
        IGBVF_MSIX_SIZE);

    pcie_dev_ser_num_init(pci_dev, IGB_PCIE_SER_CAP_OFFSET,
                          igb_gen_dsn(macaddr));

    igb_init_net_peer(s, pci_dev, macaddr);

    /* Initialize core */
    s->core.owner = &s->parent_obj;
    s->core.owner_nic = s->nic;

    igb_core_pci_realize(&s->core,
                            igb_eeprom_template,
                            sizeof(igb_eeprom_template),
                            macaddr);
}

static void igb_pci_uninit(PCIDevice *pci_dev)
{
    IgbState *s = IGB(pci_dev);

    trace_igb_cb_pci_uninit();

    igb_core_pci_uninit(&s->core);

    pcie_sriov_pf_exit(pci_dev);

    pcie_aer_exit(pci_dev);
    pcie_cap_exit(pci_dev);

    qemu_del_nic(s->nic);

    igb_cleanup_msix(s);
    msi_uninit(pci_dev);
}

static void igb_qdev_reset(DeviceState *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    IgbState *s = IGB(dev);

    trace_igb_cb_qdev_reset();

    pcie_sriov_pf_disable_vfs(pci_dev);
    igb_core_reset(&s->core);
}

static int igb_pre_save(void *opaque)
{
    IgbState *s = opaque;

    trace_igb_cb_pre_save();

    igb_core_pre_save(&s->core);

    return 0;
}

static int igb_post_load(void *opaque, int version_id)
{
    IgbState *s = opaque;

    trace_igb_cb_post_load();

    if ((s->subsys != s->subsys_used) ||
        (s->subsys_ven != s->subsys_ven_used)) {
        fprintf(stderr,
            "ERROR: Cannot migrate while device properties "
            "(subsys/subsys_ven) differ");
        return -1;
    }

    return igb_core_post_load(&s->core);
}

static PropertyInfo igb_prop_subsys_ven,
                    igb_prop_subsys;

static Property igb_properties[] = {
    DEFINE_NIC_PROPERTIES(IgbState, conf),
    DEFINE_PROP_SIGNED("subsys_ven", IgbState, subsys_ven,
                        PCI_VENDOR_ID_INTEL,
                        igb_prop_subsys_ven, uint16_t),
    DEFINE_PROP_SIGNED("subsys", IgbState, subsys, 0,
                        igb_prop_subsys, uint16_t),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription igb_vmstate = {
    .name = TYPE_IGB,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = igb_pre_save,
    .post_load = igb_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, IgbState),
        VMSTATE_MSIX(parent_obj, IgbState),

        VMSTATE_UINT32(ioaddr, IgbState),
        VMSTATE_UINT8(core.rx_desc_len, IgbState),
        VMSTATE_UINT32_ARRAY(core.rx_desc_buf_size, IgbState, IGB_NUM_QUEUES),
        VMSTATE_UINT32_ARRAY(core.rx_desc_buf_size, IgbState, IGB_NUM_QUEUES),

        VMSTATE_UINT16_ARRAY(core.eeprom, IgbState, IGB_EEPROM_SIZE),
        VMSTATE_UINT16_2DARRAY(core.phy, IgbState,
                               IGB_PHY_PAGES, IGB_PHY_PAGE_SIZE),
        VMSTATE_UINT32_ARRAY(core.mac, IgbState, IGB_MAC_SIZE),
        VMSTATE_UINT8_ARRAY(core.permanent_mac, IgbState, ETH_ALEN),

        VMSTATE_UINT32(core.delayed_causes, IgbState),

        VMSTATE_UINT16(subsys, IgbState),
        VMSTATE_UINT16(subsys_ven, IgbState),

        VMSTATE_IGB_INTR_DELAY_TIMER_ARRAY(core.eitr, IgbState,
                                              IGB_MSIX_VEC_NUM),
        VMSTATE_BOOL_ARRAY(core.eitr_intr_pending, IgbState,
                           IGB_MSIX_VEC_NUM),

        VMSTATE_UINT32_ARRAY(core.eitr_guest_value, IgbState,
                             IGB_MSIX_VEC_NUM),

        VMSTATE_UINT16(core.vet, IgbState),

        VMSTATE_STRUCT_ARRAY(core.tx_ctx, IgbState, IGB_NUM_TX_CTX, 0,
                             igb_vmstate_tx_ctx, struct igb_tx_ctx),
        VMSTATE_STRUCT_ARRAY(core.tx, IgbState, IGB_NUM_QUEUES, 0,
                             igb_vmstate_tx, struct igb_tx),
        VMSTATE_END_OF_LIST()
    }
};

static void igb_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);

    c->realize = igb_pci_realize;
    c->exit = igb_pci_uninit;
    c->vendor_id = PCI_VENDOR_ID_INTEL;
    c->device_id = E1000_DEV_ID_82576;
    c->revision = 0;
    c->romfile = NULL;
    c->class_id = PCI_CLASS_NETWORK_ETHERNET;

    dc->desc = "Intel 82576 GbE Controller";
    dc->reset = igb_qdev_reset;
    dc->vmsd = &igb_vmstate;

    igb_prop_subsys_ven = qdev_prop_uint16;
    igb_prop_subsys_ven.description = "PCI device Subsystem Vendor ID";

    igb_prop_subsys = qdev_prop_uint16;
    igb_prop_subsys.description = "PCI device Subsystem ID";

    device_class_set_props(dc, igb_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void igb_instance_init(Object *obj)
{
    IgbState *s = IGB(obj);
    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static const TypeInfo igb_info = {
    .name = TYPE_IGB,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IgbState),
    .class_init = igb_class_init,
    .instance_init = igb_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void igb_register_types(void)
{
    type_register_static(&igb_info);
}

type_init(igb_register_types)
