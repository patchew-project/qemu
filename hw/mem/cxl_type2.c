/*
 * CXL Type-2 device
 *
 * Copyright(C) 2024 NVIDIA Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/cxl/cxl.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "system/hostmem.h"

static void clean_memory(PCIDevice *pci_dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);

    address_space_destroy(&ct2d->hostvmem_as);
    host_memory_backend_set_mapped(ct2d->hostvmem, false);
}

static bool setup_memory(PCIDevice *pci_dev, Error **errp)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    MemoryRegion *vmr;

    if (!ct2d->hostvmem) {
        error_setg(errp, "volatile-memdev property must be set");
        return false;
    }
    vmr = host_memory_backend_get_memory(ct2d->hostvmem);
    if (!vmr) {
        error_setg(errp, "volatile memdev must have backing device");
        return false;
    }
    if (host_memory_backend_is_mapped(ct2d->hostvmem)) {
        error_setg(errp, "memory backend %s can't be used multiple times.",
                   object_get_canonical_path_component(OBJECT(ct2d->hostvmem)));
        return false;
    }
    memory_region_set_nonvolatile(vmr, false);
    memory_region_set_enabled(vmr, true);
    host_memory_backend_set_mapped(ct2d->hostvmem, true);
    address_space_init(&ct2d->hostvmem_as, vmr, "cxl-type2-dpa-vmem-space");
    return true;
}

static void update_dvsecs(CXLType2Dev *ct2d)
{
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;
    uint64_t size = memory_region_size(
        host_memory_backend_get_memory(ct2d->hostvmem));
    uint8_t *dvsec;

    dvsec = (uint8_t *)&(CXLDVSECDevice){
        .cap = CXL_DVSEC_CAP_IO | CXL_DVSEC_CAP_MEM |
               CXL_DVSEC_CAP_MEM_HWINIT |
               1 << CXL_DVSEC_CAP_HDM_COUNT_SHIFT,
        .ctrl = CXL_DVSEC_CTRL_IO_EN,
        .range1_size_hi = size >> 32,
        .range1_size_lo = (2 << 5) | (2 << 2) | 0x3 | (size & 0xF0000000),
    };
    cxl_component_update_dvsec(cxl_cstate, PCIE_CXL_DEVICE_DVSEC_LENGTH,
                               PCIE_CXL_DEVICE_DVSEC, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECRegisterLocator){
        .reg0_base_lo = RBI_COMPONENT_REG | CXL_COMPONENT_REG_BAR_IDX,
    };
    cxl_component_update_dvsec(cxl_cstate, REG_LOC_DVSEC_LENGTH,
                               REG_LOC_DVSEC, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortFlexBus){
        .cap                     = 0x26, /* 68B, IO, Mem, non-MLD */
        .ctrl                    = 0x02, /* IO always enabled */
        .status                  = 0x26, /* same as capabilities */
        .rcvd_mod_ts_data_phase1 = 0xef,
    };
    cxl_component_update_dvsec(cxl_cstate, PCIE_CXL3_FLEXBUS_PORT_DVSEC_LENGTH,
                               PCIE_FLEXBUS_PORT_DVSEC, dvsec);
}

static void build_dvsecs(CXLType2Dev *ct2d)
{
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;

    cxl_component_create_dvsec(cxl_cstate, CXL3_TYPE2_DEVICE,
                               PCIE_CXL_DEVICE_DVSEC_LENGTH,
                               PCIE_CXL_DEVICE_DVSEC,
                               PCIE_CXL31_DEVICE_DVSEC_REVID, NULL);
    cxl_component_create_dvsec(cxl_cstate, CXL3_TYPE2_DEVICE,
                               REG_LOC_DVSEC_LENGTH, REG_LOC_DVSEC,
                               REG_LOC_DVSEC_REVID, NULL);
    cxl_component_create_dvsec(cxl_cstate, CXL3_TYPE2_DEVICE,
                               PCIE_CXL3_FLEXBUS_PORT_DVSEC_LENGTH,
                               PCIE_FLEXBUS_PORT_DVSEC,
                               PCIE_CXL3_FLEXBUS_PORT_DVSEC_REVID, NULL);
    update_dvsecs(ct2d);

    /* No CXL.cache and no CXL Reset, so no Control2 bit is writable */
    pci_set_word(PCI_DEVICE(ct2d)->wmask +
                 range_lob(&cxl_cstate->dvsecs[PCIE_CXL_DEVICE_DVSEC]) +
                 offsetof(CXLDVSECDevice, ctrl2), 0);

    memcpy(ct2d->dvsec_wmask, PCI_DEVICE(ct2d)->wmask +
           range_lob(&cxl_cstate->dvsecs[PCIE_CXL_DEVICE_DVSEC]),
           sizeof(ct2d->dvsec_wmask));
}

/* CXL r4.0 Section 8.1.3.6: CONFIG_LOCK makes the RWL fields read only */
static void cxl_type2_dvsec_lock(CXLType2Dev *ct2d)
{
    PCIDevice *pci_dev = PCI_DEVICE(ct2d);
    uint16_t off = range_lob(&ct2d->cxl_cstate.dvsecs[PCIE_CXL_DEVICE_DVSEC]);
    uint8_t *wmask = pci_dev->wmask + off;

    if (!(pci_get_word(pci_dev->config + off + offsetof(CXLDVSECDevice, lock)) &
          CXL_DVSEC_LOCK_CONFIG_LOCK)) {
        return;
    }
    pci_set_word(wmask + offsetof(CXLDVSECDevice, ctrl), 0);
    pci_set_word(wmask + offsetof(CXLDVSECDevice, lock), 0);
    pci_set_quad(wmask + offsetof(CXLDVSECDevice, range1_base_hi), 0);
    pci_set_quad(wmask + offsetof(CXLDVSECDevice, range2_base_hi), 0);
}

static void cxl_type2_config_write(PCIDevice *pci_dev, uint32_t addr,
                                   uint32_t val, int len)
{
    pci_default_write_config(pci_dev, addr, val, len);
    cxl_type2_dvsec_lock(CXL_TYPE2(pci_dev));
}

static void setup_cxl_regs(PCIDevice *pci_dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;

    cxl_cstate->dvsec_offset = 0x100;
    cxl_cstate->pdev = pci_dev;

    build_dvsecs(ct2d);

    cxl_component_register_block_init(OBJECT(pci_dev), cxl_cstate,
                                      TYPE_CXL_TYPE2);
    pci_register_bar(pci_dev, CXL_COMPONENT_REG_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &cxl_cstate->crb.component_registers);
}

static void cxl_type2_realize(PCIDevice *pci_dev, Error **errp)
{
    if (!setup_memory(pci_dev, errp)) {
        return;
    }

    pcie_endpoint_cap_init(pci_dev, 0x80);

    setup_cxl_regs(pci_dev);
}

static void cxl_type2_exit(PCIDevice *pci_dev)
{
    clean_memory(pci_dev);
}

static void cxl_type2_reset(DeviceState *dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(dev);
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;

    update_dvsecs(ct2d);
    memcpy(PCI_DEVICE(ct2d)->wmask +
           range_lob(&cxl_cstate->dvsecs[PCIE_CXL_DEVICE_DVSEC]),
           ct2d->dvsec_wmask, sizeof(ct2d->dvsec_wmask));
    cxl_component_register_init_count(cxl_cstate->crb.cache_mem_registers,
                                      cxl_cstate->crb.cache_mem_regs_write_mask,
                                      CXL3_TYPE2_DEVICE, false, 1);
}

static const Property cxl_type2_props[] = {
    DEFINE_PROP_LINK("volatile-memdev", CXLType2Dev, hostvmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
};

static const VMStateDescription cxl_type2_vmsd = {
    .name = TYPE_CXL_TYPE2,
    .unmigratable = 1,
};

static void cxl_type2_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = cxl_type2_realize;
    pc->exit = cxl_type2_exit;
    pc->config_write = cxl_type2_config_write;

    pc->class_id = PCI_CLASS_ACCELERATOR_PROCESSING;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_CXL_TYPE2;
    pc->revision = 1;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "CXL Type-2 device";
    dc->vmsd = &cxl_type2_vmsd;
    device_class_set_legacy_reset(dc, cxl_type2_reset);
    device_class_set_props(dc, cxl_type2_props);
}

static const TypeInfo cxl_type2_dev_info = {
    .name = TYPE_CXL_TYPE2,
    .parent = TYPE_PCI_DEVICE,
    .class_init = cxl_type2_class_init,
    .instance_size = sizeof(CXLType2Dev),
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CXL_DEVICE },
        { INTERFACE_PCIE_DEVICE },
        {}
    },
};

static void cxl_type2_dev_registers(void)
{
    type_register_static(&cxl_type2_dev_info);
}

type_init(cxl_type2_dev_registers);
