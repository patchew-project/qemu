/*
 * Generic PCI Express Root Port emulation
 *
 * Copyright (C) 2017 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie_port.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define TYPE_GEN_PCIE_ROOT_PORT                "pcie-root-port"
OBJECT_DECLARE_SIMPLE_TYPE(GenPCIERootPort, GEN_PCIE_ROOT_PORT)

#define GEN_PCIE_ROOT_PORT_AER_OFFSET           0x100
#define GEN_PCIE_ROOT_PORT_ACS_OFFSET \
        (GEN_PCIE_ROOT_PORT_AER_OFFSET + PCI_ERR_SIZEOF)
#define GEN_PCIE_ROOT_PORT_ACS_END \
        (GEN_PCIE_ROOT_PORT_ACS_OFFSET + PCI_ACS_SIZEOF)

#define GEN_PCIE_ROOT_PORT_MSIX_NR_VECTOR       1
#define GEN_PCIE_ROOT_DEFAULT_IO_RANGE          4096

struct GenPCIERootPort {
    /*< private >*/
    PCIESlot parent_obj;
    /*< public >*/

    /* additional resources to reserve */
    PCIResReserve res_reserve;
};

static uint8_t gen_rp_aer_vector(const PCIDevice *d)
{
    return 0;
}

static int gen_rp_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msix_init_exclusive_bar(d, GEN_PCIE_ROOT_PORT_MSIX_NR_VECTOR, 0, errp);

    if (rc < 0) {
        assert(rc == -ENOTSUP);
    } else {
        msix_vector_use(d, 0);
    }

    return rc;
}

static void gen_rp_interrupts_uninit(PCIDevice *d)
{
    msix_uninit_exclusive_bar(d);
}

static void gen_rp_realize(DeviceState *dev, Error **errp)
{
    PCIDevice *d = PCI_DEVICE(dev);
    PCIESlot *s = PCIE_SLOT(d);
    GenPCIERootPort *grp = GEN_PCIE_ROOT_PORT(d);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);
    Error *local_err = NULL;

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * reserving IO space led to worse issues in 6.1, when this hunk was
     * introduced. (see commit: 211afe5c69b59). Keep this broken for 6.1
     * machine type ABI compatibility only
     */
    if (s->hide_native_hotplug_cap && grp->res_reserve.io == -1 && s->hotplug) {
        grp->res_reserve.io = GEN_PCIE_ROOT_DEFAULT_IO_RANGE;
    }
    int rc = pci_bridge_qemu_reserve_cap_init(d, 0,
                                              grp->res_reserve, errp);

    if (rc < 0) {
        rpc->parent_class.exit(d);
        return;
    }

    if (!grp->res_reserve.io) {
        pci_word_test_and_clear_mask(d->wmask + PCI_COMMAND,
                                     PCI_COMMAND_IO);
        d->wmask[PCI_IO_BASE] = 0;
        d->wmask[PCI_IO_LIMIT] = 0;
    }

    uint32_t offset = GEN_PCIE_ROOT_PORT_ACS_END;
    /*
     * dvsec rme-da
     * https://developer.arm.com/documentation/den0129/latest/
     * Arm RME System Architecture
     * 0x0000 RMEDA_ECH See B3.2.6.2.1 RME-DA Extended Capability Header
     * 0x0004 RMEDA_HEAD1 See B3.2.6.2.2 RME-DA DVSEC Header 1
     * 0x0008 RMEDA_HEAD2 See B3.2.6.2.3 RME-DA DVSEC Header 2
     * 0x000C RMEDA_CTL1 See B3.2.6.2.4 RME-DA Control register 1
     * 0x0010 RMEDA_CTL2 See B3.2.6.2.5 RME-DA Control register 2
     */
    pcie_add_capability(d, PCI_EXT_CAP_ID_DVSEC, PCI_RMEDA_VER, offset,
                        PCI_RMEDA_SIZEOF);
    const uint32_t header1 = 0x010013b5;
    const uint32_t header2 = 0xFF01;
    const uint32_t ctl1 = 0x1; /* support tdisp */
    const uint32_t ctl2 = 0x0; /* unlocked */
    pci_set_long(d->config + offset + 0x4, header1);
    pci_set_long(d->config + offset + 0x8, header2);
    pci_set_long(d->config + offset + 0xC, ctl1);
    pci_set_long(d->config + offset + 0x10, ctl2);
    d->wmask[offset + 0xC] = 0xff;
    d->wmask[offset + 0xC + 1] = 0xff;
    d->wmask[offset + 0xC + 2] = 0xff;
    d->wmask[offset + 0xC + 3] = 0xff;
    d->wmask[offset + 0x10] = 0xff;
    d->wmask[offset + 0x10 + 1] = 0xff;
    d->wmask[offset + 0x10 + 2] = 0xff;
    d->wmask[offset + 0x10 + 3] = 0xff;
    offset += PCI_RMEDA_SIZEOF;

    pcie_ide_init(d, offset);
    offset += PCI_IDE_SIZEOF;

    pcie_cap_tee_init(d);
}

static const VMStateDescription vmstate_rp_dev = {
    .name = "pcie-root-port",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_MSIX(parent_obj.parent_obj.parent_obj.parent_obj,
                     GenPCIERootPort),
        VMSTATE_END_OF_LIST()
    }
};

static const Property gen_rp_props[] = {
    DEFINE_PROP_UINT32("bus-reserve", GenPCIERootPort,
                       res_reserve.bus, -1),
    DEFINE_PROP_SIZE("io-reserve", GenPCIERootPort,
                     res_reserve.io, -1),
    DEFINE_PROP_SIZE("mem-reserve", GenPCIERootPort,
                     res_reserve.mem_non_pref, -1),
    DEFINE_PROP_SIZE("pref32-reserve", GenPCIERootPort,
                     res_reserve.mem_pref_32, -1),
    DEFINE_PROP_SIZE("pref64-reserve", GenPCIERootPort,
                     res_reserve.mem_pref_64, -1),
    DEFINE_PROP_PCIE_LINK_SPEED("x-speed", PCIESlot,
                                speed, PCIE_LINK_SPEED_16),
    DEFINE_PROP_PCIE_LINK_WIDTH("x-width", PCIESlot,
                                width, PCIE_LINK_WIDTH_32),
};

static void gen_rp_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_RP;
    dc->desc = "PCI Express Root Port";
    dc->vmsd = &vmstate_rp_dev;
    device_class_set_props(dc, gen_rp_props);

    device_class_set_parent_realize(dc, gen_rp_realize, &rpc->parent_realize);

    rpc->aer_vector = gen_rp_aer_vector;
    rpc->interrupts_init = gen_rp_interrupts_init;
    rpc->interrupts_uninit = gen_rp_interrupts_uninit;
    rpc->aer_offset = GEN_PCIE_ROOT_PORT_AER_OFFSET;
    rpc->acs_offset = GEN_PCIE_ROOT_PORT_ACS_OFFSET;
}

static const TypeInfo gen_rp_dev_info = {
    .name          = TYPE_GEN_PCIE_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(GenPCIERootPort),
    .class_init    = gen_rp_dev_class_init,
};

 static void gen_rp_register_types(void)
 {
    type_register_static(&gen_rp_dev_info);
 }
 type_init(gen_rp_register_types)
