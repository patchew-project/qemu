/*
 * CXL Type-2 device
 *
 * Copyright(C) 2024 NVIDIA Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_host.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_port.h"
#include "migration/vmstate.h"
#include "system/hostmem.h"
#include "system/ramblock.h"

static void clean_memory(PCIDevice *pci_dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);

    address_space_destroy(&ct2d->hostvmem_as);
    host_memory_backend_set_mapped(ct2d->hostvmem, false);
}

/*
 * The firmware decoder setup covers only the host bridge and the device, and
 * cxl_cfmws_find_device() routes only to devfn 0 below a port.
 */
static bool cxl_type2_check_topology(PCIDevice *pci_dev, Error **errp)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    PCIDevice *rp = pci_get_bus(pci_dev)->parent_dev;
    PCIDevice *host = rp ? pci_get_bus(rp)->parent_dev : NULL;

    if (!rp || !pci_is_cxl(rp) ||
        !object_dynamic_cast(OBJECT(rp), TYPE_PCIE_ROOT_PORT) || !host ||
        !object_dynamic_cast(OBJECT(host), TYPE_PXB_CXL_DEV)) {
        error_setg(errp, "cxl-type2 must be directly below a CXL root port "
                   "of a pxb-cxl");
        return false;
    }
    if (pci_dev->devfn != 0) {
        error_setg(errp, "cxl-type2 must be at slot 0, function 0");
        return false;
    }

    ct2d->pxb = PXB_CXL_DEV(host);
    ct2d->root_port = PCIE_PORT(rp)->port;
    return true;
}

/* fw->target_hbs is set only once machine creation is done */
static bool cxl_type2_resolve_fmw(CXLType2Dev *ct2d, Error **errp)
{
    MemoryRegion *mr = host_memory_backend_get_memory(ct2d->hostvmem);
    GSList *list = cxl_fmws_get_all_sorted();
    CXLFixedWindow *fw = NULL;
    unsigned int matches = 0;
    bool ambiguous = false;
    Object *target;

    for (GSList *iter = list; iter; iter = iter->next) {
        if (CXL_FMW(iter->data)->index == ct2d->fmw_index) {
            fw = CXL_FMW(iter->data);
            matches++;
        }
    }
    g_slist_free(list);

    if (matches != 1) {
        error_setg(errp, "cxl-type2 fmw-index %u matched %u CFMWS windows",
                   ct2d->fmw_index, matches);
        return false;
    }
    if (fw->num_targets != 1) {
        error_setg(errp, "cxl-type2 requires a single-target CFMWS");
        return false;
    }
    if (!(fw->restrictions & CXL_FMW_DEVICE_COHERENT)) {
        error_setg(errp, "cxl-type2 CFMWS must allow device-coherent memory");
        return false;
    }
    if (memory_region_size(mr) > fw->size) {
        error_setg(errp, "cxl-type2 volatile-memdev does not fit in its CFMWS");
        return false;
    }
    target = object_resolve_path_type(fw->targets[0], TYPE_PXB_CXL_DEV,
                                      &ambiguous);
    if (!target || ambiguous || target != OBJECT(ct2d->pxb)) {
        error_setg(errp, "cxl-type2 CFMWS does not target its pxb-cxl");
        return false;
    }

    ct2d->fmw = fw;
    return true;
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
    if (!memory_region_size(vmr) ||
        !QEMU_IS_ALIGNED(memory_region_size(vmr), 256 * MiB)) {
        error_setg(errp, "volatile-memdev size must be a nonzero multiple of "
                   "256 MiB");
        return false;
    }
    memory_region_set_nonvolatile(vmr, false);
    memory_region_set_enabled(vmr, true);
    host_memory_backend_set_mapped(ct2d->hostvmem, true);
    address_space_init(&ct2d->hostvmem_as, vmr, "cxl-type2-dpa-vmem-space");
    return true;
}

static uint16_t cxl_type2_dvsec(CXLType2Dev *ct2d, size_t reg)
{
    return range_lob(&ct2d->cxl_cstate.dvsecs[PCIE_CXL_DEVICE_DVSEC]) + reg;
}

/*
 * CXL.mem needs Mem_Enable (CXL r4.0 Table 8-6) and HDM Decoder Enable
 * (Table 8-118); the device does not decode through the DVSEC ranges.
 */
static bool cxl_type2_mem_active(CXLType2Dev *ct2d)
{
    uint16_t ctrl = pci_get_word(PCI_DEVICE(ct2d)->config +
                                 cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice,
                                                                ctrl)));

    return (ctrl & CXL_DVSEC_CTRL_MEM_EN) &&
           ARRAY_FIELD_EX32(ct2d->cxl_cstate.crb.cache_mem_registers,
                            CXL_HDM_DECODER_GLOBAL_CONTROL,
                            HDM_DECODER_ENABLE);
}

static void cxl_type2_update_mem_active(CXLType2Dev *ct2d)
{
    bool active = cxl_type2_mem_active(ct2d);

    for (int i = 0; i < CXL_HDM_DECODER_COUNT; i++) {
        if (ct2d->direct_mr_fw[i]) {
            memory_region_set_enabled(&ct2d->direct_mr[i], active);
        }
    }
}

static int cxl_type2_hpa_to_as_and_dpa(CXLType2Dev *ct2d, hwaddr host_addr,
                                       AddressSpace **as, uint64_t *dpa)
{
    MemoryRegion *vmr = host_memory_backend_get_memory(ct2d->hostvmem);

    if (!cxl_type2_mem_active(ct2d) ||
        !cxl_host_addr_to_dpa(&ct2d->cxl_cstate, host_addr, dpa) ||
        *dpa >= memory_region_size(vmr)) {
        return -EINVAL;
    }
    *as = &ct2d->hostvmem_as;
    return 0;
}

MemTxResult cxl_type2_read(PCIDevice *d, hwaddr host_addr, uint64_t *data,
                           unsigned size, MemTxAttrs attrs)
{
    AddressSpace *as;
    uint64_t dpa;

    if (cxl_type2_hpa_to_as_and_dpa(CXL_TYPE2(d), host_addr, &as, &dpa)) {
        *data = 0;
        return MEMTX_ERROR;
    }
    return address_space_read(as, dpa, attrs, data, size);
}

MemTxResult cxl_type2_write(PCIDevice *d, hwaddr host_addr, uint64_t data,
                            unsigned size, MemTxAttrs attrs)
{
    AddressSpace *as;
    uint64_t dpa;

    if (cxl_type2_hpa_to_as_and_dpa(CXL_TYPE2(d), host_addr, &as, &dpa)) {
        return MEMTX_ERROR;
    }
    return address_space_write(as, dpa, attrs, &data, size);
}

static void update_dvsecs(CXLType2Dev *ct2d)
{
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;
    uint64_t size = memory_region_size(
        host_memory_backend_get_memory(ct2d->hostvmem));
    uint8_t *dvsec;

    dvsec = (uint8_t *)&(CXLDVSECDevice){
        .cap = CXL_DVSEC_CAP_CACHE | CXL_DVSEC_CAP_IO | CXL_DVSEC_CAP_MEM |
               CXL_DVSEC_CAP_MEM_HWINIT |
               1 << CXL_DVSEC_CAP_HDM_COUNT_SHIFT | CXL_DVSEC_CAP_CACHE_WBI |
               CXL_DVSEC_CAP_RST | CXL_DVSEC_CAP_RST_MEM_CLR,
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
        .cap                     = 0x27, /* 68B, IO, Cache, Mem, non-MLD */
        .ctrl                    = 0x02, /* IO always enabled */
        .status                  = 0x27, /* same as capabilities */
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

    pci_set_word(PCI_DEVICE(ct2d)->wmask +
                 cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, ctrl2)),
                 CXL_DVSEC_CTRL2_DISABLE_CACHING |
                 CXL_DVSEC_CTRL2_INIT_CACHE_WBI |
                 CXL_DVSEC_CTRL2_INIT_CXL_RST |
                 CXL_DVSEC_CTRL2_RST_MEM_CLR_EN);

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

static void cxl_type2_update_status2(CXLType2Dev *ct2d, uint16_t clear,
                                     uint16_t set)
{
    uint8_t *reg = PCI_DEVICE(ct2d)->config +
                   cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, status2));

    pci_set_word(reg, (pci_get_word(reg) & ~clear) | set);
}

/*
 * A discard would change the file behind a private file mapping and undo
 * the preallocation of a prealloc backend, so those get zeros written.
 */
static bool cxl_type2_clear_memory(CXLType2Dev *ct2d)
{
    MemoryRegion *vmr = host_memory_backend_get_memory(ct2d->hostvmem);
    RAMBlock *rb = vmr->ram_block;
    uint64_t size = memory_region_size(vmr);

    if (rb && !ct2d->hostvmem->prealloc && !ram_block_discard_is_disabled() &&
        (qemu_ram_get_fd(rb) < 0 || qemu_ram_is_shared(rb)) &&
        !ram_block_discard_range(rb, 0, size)) {
        return true;
    }
    return address_space_set(&ct2d->hostvmem_as, 0, 0, size,
                             MEMTXATTRS_UNSPECIFIED) == MEMTX_OK;
}

/*
 * CXL r4.0 Section 9.7. Fields locked by CONFIG_LOCK or Lock On Commit keep
 * their values. The CXL.io part of the reset, which has the scope of an FLR,
 * is not emulated.
 */
static void cxl_type2_cxl_reset(CXLType2Dev *ct2d, uint16_t ctrl2)
{
    static const size_t range_bases[] = {
        offsetof(CXLDVSECDevice, range1_base_hi),
        offsetof(CXLDVSECDevice, range1_base_lo),
        offsetof(CXLDVSECDevice, range2_base_hi),
        offsetof(CXLDVSECDevice, range2_base_lo),
    };
    uint8_t *config = PCI_DEVICE(ct2d)->config;
    uint32_t *cache_mem = ct2d->cxl_cstate.crb.cache_mem_registers;
    uint16_t lock = cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, lock));
    bool cleared = true;

    cxl_type2_update_status2(ct2d, CXL_DVSEC_STATUS2_RST_DONE |
                             CXL_DVSEC_STATUS2_RST_ERR, 0);

    cache_mem[R_CXL_HDM_DECODER_GLOBAL_CONTROL] = 0;

    if (!(pci_get_word(config + lock) & CXL_DVSEC_LOCK_CONFIG_LOCK)) {
        pci_set_word(config + cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice,
                                                             ctrl)),
                     CXL_DVSEC_CTRL_IO_EN);
        for (int i = 0; i < ARRAY_SIZE(range_bases); i++) {
            pci_set_long(config + cxl_type2_dvsec(ct2d, range_bases[i]), 0);
        }
    }
    pci_set_word(config + cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice,
                                                         ctrl2)), 0);

    cxl_type2_update_mem_active(ct2d);

    if (ctrl2 & CXL_DVSEC_CTRL2_RST_MEM_CLR_EN) {
        cleared = cxl_type2_clear_memory(ct2d);
    }
    cxl_type2_update_status2(ct2d, CXL_DVSEC_STATUS2_CACHE_INV,
                             cleared ? CXL_DVSEC_STATUS2_RST_DONE :
                             CXL_DVSEC_STATUS2_RST_ERR);
}

/*
 * CXL r4.0 Section 8.1.3.4: the device holds no modified cache lines, so a
 * write back and invalidate completes at once.
 */
static void cxl_type2_ctrl2_write(CXLType2Dev *ct2d)
{
    uint8_t *reg = PCI_DEVICE(ct2d)->config +
                   cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, ctrl2));
    uint16_t ctrl2 = pci_get_word(reg);

    if (!(ctrl2 & CXL_DVSEC_CTRL2_DISABLE_CACHING)) {
        /* Cache Invalid reads as 0 while Disable Caching is 0 */
        cxl_type2_update_status2(ct2d, CXL_DVSEC_STATUS2_CACHE_INV, 0);
    }
    if (ctrl2 & CXL_DVSEC_CTRL2_INIT_CACHE_WBI) {
        pci_set_word(reg, ctrl2 & ~CXL_DVSEC_CTRL2_INIT_CACHE_WBI);
        /* Undefined unless Disable Caching is set */
        if (ctrl2 & CXL_DVSEC_CTRL2_DISABLE_CACHING) {
            cxl_type2_update_status2(ct2d, 0, CXL_DVSEC_STATUS2_CACHE_INV);
        }
    }
    /* Initiate CXL Reset reads as 0; the reset clears all of Control2 */
    if (ctrl2 & CXL_DVSEC_CTRL2_INIT_CXL_RST) {
        cxl_type2_cxl_reset(ct2d, ctrl2);
    }
}

static void cxl_type2_config_write(PCIDevice *pci_dev, uint32_t addr,
                                   uint32_t val, int len)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    uint16_t ctrl = cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, ctrl));
    uint16_t ctrl2 = cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, ctrl2));

    pci_default_write_config(pci_dev, addr, val, len);
    cxl_type2_dvsec_lock(ct2d);
    if (ranges_overlap(addr, len, ctrl, sizeof(uint16_t))) {
        cxl_type2_update_mem_active(ct2d);
    }
    if (ranges_overlap(addr, len, ctrl2, sizeof(uint16_t))) {
        cxl_type2_ctrl2_write(ct2d);
    }
}

static void cxl_type2_reg_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    CXLComponentState *cxl_cstate = opaque;

    cxl_cache_mem_write_common(cxl_cstate, offset, value);
    if (offset == A_CXL_HDM_DECODER_GLOBAL_CONTROL) {
        cxl_type2_update_mem_active(container_of(cxl_cstate, CXLType2Dev,
                                                 cxl_cstate));
    }
}

static void setup_cxl_regs(PCIDevice *pci_dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;

    cxl_cstate->dvsec_offset = 0x100;
    cxl_cstate->pdev = pci_dev;

    build_dvsecs(ct2d);

    cxl_cstate->crb.special_ops = g_new0(MemoryRegionOps, 1);
    cxl_cstate->crb.special_ops->write = cxl_type2_reg_write;
    cxl_component_register_block_init(OBJECT(pci_dev), cxl_cstate,
                                      TYPE_CXL_TYPE2);
    pci_register_bar(pci_dev, CXL_COMPONENT_REG_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &cxl_cstate->crb.component_registers);
}

static void cxl_type2_realize(PCIDevice *pci_dev, Error **errp)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    uint64_t size;

    if (!cxl_type2_check_topology(pci_dev, errp) ||
        !setup_memory(pci_dev, errp)) {
        return;
    }
    if (!cxl_type2_resolve_fmw(ct2d, errp)) {
        clean_memory(pci_dev);
        return;
    }
    size = memory_region_size(host_memory_backend_get_memory(ct2d->hostvmem));

    pcie_endpoint_cap_init(pci_dev, 0x80);

    setup_cxl_regs(pci_dev);

    if (!pxb_cxl_register_firmware_hdm(ct2d->pxb, ct2d->fmw->base, size,
                                       ct2d->root_port, errp)) {
        g_free(ct2d->cxl_cstate.crb.special_ops);
        clean_memory(pci_dev);
    }
}

static void cxl_type2_exit(PCIDevice *pci_dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);

    pxb_cxl_unregister_firmware_hdm(ct2d->pxb);
    cxl_fmws_direct_passthrough_remove_all(pci_dev);
    g_free(ct2d->cxl_cstate.crb.special_ops);
    clean_memory(pci_dev);
}

/* CXL r4.0 Section 8.2.4.20.7: a locked decoder is read only until reset */
static void cxl_type2_lock_decoder(CXLComponentState *cxl_cstate)
{
    uint32_t *write_msk = cxl_cstate->crb.cache_mem_regs_write_mask;

    write_msk[R_CXL_HDM_DECODER0_BASE_LO] = 0;
    write_msk[R_CXL_HDM_DECODER0_BASE_HI] = 0;
    write_msk[R_CXL_HDM_DECODER0_SIZE_LO] = 0;
    write_msk[R_CXL_HDM_DECODER0_SIZE_HI] = 0;
    write_msk[R_CXL_HDM_DECODER0_CTRL] = 0;
    write_msk[R_CXL_HDM_DECODER0_DPA_SKIP_LO] = 0;
    write_msk[R_CXL_HDM_DECODER0_DPA_SKIP_HI] = 0;
}

static void cxl_type2_firmware_init(CXLType2Dev *ct2d)
{
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;
    uint32_t *cache_mem = cxl_cstate->crb.cache_mem_registers;
    uint16_t ctrl = cxl_type2_dvsec(ct2d, offsetof(CXLDVSECDevice, ctrl));
    CXLFirmwareHDM range = {
        .valid = true,
        .base = ct2d->fmw->base,
        .size = memory_region_size(
            host_memory_backend_get_memory(ct2d->hostvmem)),
    };

    pci_set_word(PCI_DEVICE(ct2d)->config + ctrl,
                 CXL_DVSEC_CTRL_CACHE_EN | CXL_DVSEC_CTRL_IO_EN |
                 CXL_DVSEC_CTRL_MEM_EN);
    ARRAY_FIELD_DP32(cache_mem, CXL_HDM_DECODER_GLOBAL_CONTROL,
                     HDM_DECODER_ENABLE, 1);
    cxl_firmware_hdm_program(&range, cache_mem);
    ARRAY_FIELD_DP32(cache_mem, CXL_HDM_DECODER0_CTRL, LOCK_ON_COMMIT, 1);
    cxl_type2_lock_decoder(cxl_cstate);
    cfmws_update_non_interleaved(true);
}

static void cxl_type2_reset(DeviceState *dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(dev);
    CXLComponentState *cxl_cstate = &ct2d->cxl_cstate;

    cxl_fmws_direct_passthrough_remove_all(PCI_DEVICE(dev));
    update_dvsecs(ct2d);
    memcpy(PCI_DEVICE(ct2d)->wmask +
           range_lob(&cxl_cstate->dvsecs[PCIE_CXL_DEVICE_DVSEC]),
           ct2d->dvsec_wmask, sizeof(ct2d->dvsec_wmask));
    cxl_component_register_init_count(cxl_cstate->crb.cache_mem_registers,
                                      cxl_cstate->crb.cache_mem_regs_write_mask,
                                      CXL3_TYPE2_DEVICE, false, 1);

    if (ct2d->fmw) {
        cxl_type2_firmware_init(ct2d);
    }
}

static const Property cxl_type2_props[] = {
    DEFINE_PROP_LINK("volatile-memdev", CXLType2Dev, hostvmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_UINT32("fmw-index", CXLType2Dev, fmw_index, 0),
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
    /* pxb-cxl commits its decoder for the device only at reset */
    dc->hotpluggable = false;
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
