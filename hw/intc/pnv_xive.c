/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/dma.h"
#include "monitor/monitor.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_xive.h"
#include "hw/ppc/xive_regs.h"
#include "hw/ppc/ppc.h"

#include <libfdt.h>

#include "pnv_xive_regs.h"

/*
 * Interrupt source number encoding
 */
#define SRCNO_BLOCK(srcno)        (((srcno) >> 28) & 0xf)
#define SRCNO_INDEX(srcno)        ((srcno) & 0x0fffffff)
#define XIVE_SRCNO(blk, idx)      ((uint32_t)(blk) << 28 | (idx))

/*
 * Virtual structures table accessors
 */
typedef struct XiveVstInfo {
    const char *name;
    uint32_t    size;
    uint32_t    max_blocks;
} XiveVstInfo;

static const XiveVstInfo vst_infos[] = {
    [VST_TSEL_IVT]  = { "EAT",  sizeof(XiveEAS), 16 },
    [VST_TSEL_SBE]  = { "SBE",  0,               16 },
    [VST_TSEL_EQDT] = { "ENDT", sizeof(XiveEND), 16 },
    [VST_TSEL_VPDT] = { "VPDT", sizeof(XiveNVT),  32 },

    /* Interrupt fifo backing store table :
     *
     * 0 - IPI,
     * 1 - HWD,
     * 2 - First escalate,
     * 3 - Second escalate,
     * 4 - Redistribution,
     * 5 - IPI cascaded queue ?
     */
    [VST_TSEL_IRQ]  = { "IRQ",  0,               6  },
};

#define xive_error(xive, fmt, ...)                                      \
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE[%x] - " fmt "\n", (xive)->chip_id, \
                  ## __VA_ARGS__);

/*
 * Our lookup routine for a remote XIVE IC. A simple scan of the chips.
 */
static PnvXive *pnv_xive_get_ic(PnvXive *xive, uint8_t blk)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv9Chip *chip9 = PNV9_CHIP(pnv->chips[i]);
        PnvXive *ic_xive = &chip9->xive;
        bool chip_override =
            ic_xive->regs[PC_GLOBAL_CONFIG >> 3] & PC_GCONF_CHIPID_OVR;

        if (chip_override) {
            if (ic_xive->chip_id == blk) {
                return ic_xive;
            }
        } else {
            ; /* TODO: Block scope support */
        }
    }
    xive_error(xive, "VST: unknown chip/block %d !?", blk);
    return NULL;
}

/*
 * Virtual Structures Table accessors for SBE, EAT, ENDT, NVT
 */
static uint64_t pnv_xive_vst_addr_direct(PnvXive *xive,
                                         const XiveVstInfo *info, uint64_t vsd,
                                         uint8_t blk, uint32_t idx)
{
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;
    uint64_t vst_tsize = 1ull << (GETFIELD(VSD_TSIZE, vsd) + 12);
    uint32_t idx_max = (vst_tsize / info->size) - 1;

    if (idx > idx_max) {
#ifdef XIVE_DEBUG
        xive_error(xive, "VST: %s entry %x/%x out of range !?", info->name,
                   blk, idx);
#endif
        return 0;
    }

    return vst_addr + idx * info->size;
}

#define XIVE_VSD_SIZE 8

static uint64_t pnv_xive_vst_addr_indirect(PnvXive *xive,
                                           const XiveVstInfo *info,
                                           uint64_t vsd, uint8_t blk,
                                           uint32_t idx)
{
    uint64_t vsd_addr;
    uint64_t vst_addr;
    uint32_t page_shift;
    uint32_t page_mask;
    uint64_t vst_tsize = 1ull << (GETFIELD(VSD_TSIZE, vsd) + 12);
    uint32_t idx_max = (vst_tsize / XIVE_VSD_SIZE) - 1;

    if (idx > idx_max) {
#ifdef XIVE_DEBUG
        xive_error(xive, "VET: %s entry %x/%x out of range !?", info->name,
                   blk, idx);
#endif
        return 0;
    }

    vsd_addr = vsd & VSD_ADDRESS_MASK;

    /*
     * Read the first descriptor to get the page size of each indirect
     * table.
     */
    vsd = ldq_be_dma(&address_space_memory, vsd_addr);
    page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;
    page_mask = (1ull << page_shift) - 1;

    /* Indirect page size can be 4K, 64K, 2M. */
    if (page_shift != 12 && page_shift != 16 && page_shift != 23) {
        xive_error(xive, "VST: invalid %s table shift %d", info->name,
                   page_shift);
    }

    if (!(vsd & VSD_ADDRESS_MASK)) {
        xive_error(xive, "VST: invalid %s entry %x/%x !?", info->name,
                   blk, 0);
        return 0;
    }

    /* Load the descriptor we are looking for, if not already done */
    if (idx) {
        vsd_addr = vsd_addr + (idx >> page_shift);
        vsd = ldq_be_dma(&address_space_memory, vsd_addr);

        if (page_shift != GETFIELD(VSD_TSIZE, vsd) + 12) {
            xive_error(xive, "VST: %s entry %x/%x indirect page size differ !?",
                       info->name, blk, idx);
            return 0;
        }
    }

    vst_addr = vsd & VSD_ADDRESS_MASK;

    return vst_addr + (idx & page_mask) * info->size;
}

static uint64_t pnv_xive_vst_addr(PnvXive *xive, uint8_t type, uint8_t blk,
                                  uint32_t idx)
{
    uint64_t vsd;

    if (blk >= vst_infos[type].max_blocks) {
        xive_error(xive, "VST: invalid block id %d for VST %s %d !?",
                   blk, vst_infos[type].name, idx);
        return 0;
    }

    vsd = xive->vsds[type][blk];

    /* Remote VST accesses */
    if (GETFIELD(VSD_MODE, vsd) == VSD_MODE_FORWARD) {
        xive = pnv_xive_get_ic(xive, blk);

        return xive ? pnv_xive_vst_addr(xive, type, blk, idx) : 0;
    }

    if (VSD_INDIRECT & vsd) {
        return pnv_xive_vst_addr_indirect(xive, &vst_infos[type], vsd,
                                          blk, idx);
    }

    return pnv_xive_vst_addr_direct(xive, &vst_infos[type], vsd, blk, idx);
}

static int pnv_xive_get_end(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                           XiveEND *end)
{
    PnvXive *xive = PNV_XIVE(xrtr);
    uint64_t end_addr = pnv_xive_vst_addr(xive, VST_TSEL_EQDT, blk, idx);

    if (!end_addr) {
        return -1;
    }

    cpu_physical_memory_read(end_addr, end, sizeof(XiveEND));
    end->w0 = be32_to_cpu(end->w0);
    end->w1 = be32_to_cpu(end->w1);
    end->w2 = be32_to_cpu(end->w2);
    end->w3 = be32_to_cpu(end->w3);
    end->w4 = be32_to_cpu(end->w4);
    end->w5 = be32_to_cpu(end->w5);
    end->w6 = be32_to_cpu(end->w6);
    end->w7 = be32_to_cpu(end->w7);

    return 0;
}

static int pnv_xive_set_end(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                           XiveEND *in_end)
{
    PnvXive *xive = PNV_XIVE(xrtr);
    XiveEND end;
    uint64_t end_addr = pnv_xive_vst_addr(xive, VST_TSEL_EQDT, blk, idx);

    if (!end_addr) {
        return -1;
    }

    end.w0 = cpu_to_be32(in_end->w0);
    end.w1 = cpu_to_be32(in_end->w1);
    end.w2 = cpu_to_be32(in_end->w2);
    end.w3 = cpu_to_be32(in_end->w3);
    end.w4 = cpu_to_be32(in_end->w4);
    end.w5 = cpu_to_be32(in_end->w5);
    end.w6 = cpu_to_be32(in_end->w6);
    end.w7 = cpu_to_be32(in_end->w7);
    cpu_physical_memory_write(end_addr, &end, sizeof(XiveEND));
    return 0;
}

static int pnv_xive_end_update(PnvXive *xive, uint8_t blk, uint32_t idx)
{
    uint64_t end_addr = pnv_xive_vst_addr(xive, VST_TSEL_EQDT, blk, idx);

    if (!end_addr) {
        return -1;
    }

    cpu_physical_memory_write(end_addr, xive->eqc_watch, sizeof(XiveEND));
    return 0;
}

static int pnv_xive_get_nvt(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                           XiveNVT *nvt)
{
    PnvXive *xive = PNV_XIVE(xrtr);
    uint64_t nvt_addr = pnv_xive_vst_addr(xive, VST_TSEL_VPDT, blk, idx);

    if (!nvt_addr) {
        return -1;
    }

    cpu_physical_memory_read(nvt_addr, nvt, sizeof(XiveNVT));
    nvt->w0 = cpu_to_be32(nvt->w0);
    nvt->w1 = cpu_to_be32(nvt->w1);
    nvt->w2 = cpu_to_be32(nvt->w2);
    nvt->w3 = cpu_to_be32(nvt->w3);
    nvt->w4 = cpu_to_be32(nvt->w4);
    nvt->w5 = cpu_to_be32(nvt->w5);
    nvt->w6 = cpu_to_be32(nvt->w6);
    nvt->w7 = cpu_to_be32(nvt->w7);

    return 0;
}

static int pnv_xive_set_nvt(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                           XiveNVT *in_nvt)
{
    PnvXive *xive = PNV_XIVE(xrtr);
    XiveNVT nvt;
    uint64_t nvt_addr = pnv_xive_vst_addr(xive, VST_TSEL_VPDT, blk, idx);

    if (!nvt_addr) {
        return -1;
    }

    nvt.w0 = cpu_to_be32(in_nvt->w0);
    nvt.w1 = cpu_to_be32(in_nvt->w1);
    nvt.w2 = cpu_to_be32(in_nvt->w2);
    nvt.w3 = cpu_to_be32(in_nvt->w3);
    nvt.w4 = cpu_to_be32(in_nvt->w4);
    nvt.w5 = cpu_to_be32(in_nvt->w5);
    nvt.w6 = cpu_to_be32(in_nvt->w6);
    nvt.w7 = cpu_to_be32(in_nvt->w7);
    cpu_physical_memory_write(nvt_addr, &nvt, sizeof(XiveNVT));
    return 0;
}

static int pnv_xive_nvt_update(PnvXive *xive, uint8_t blk, uint32_t idx)
{
    uint64_t nvt_addr = pnv_xive_vst_addr(xive, VST_TSEL_VPDT, blk, idx);

    if (!nvt_addr) {
        return -1;
    }

    cpu_physical_memory_write(nvt_addr, xive->vpc_watch, sizeof(XiveNVT));
    return 0;
}

static int pnv_xive_get_eas(XiveRouter *xrtr, uint32_t srcno, XiveEAS *eas)
{
    PnvXive *xive = PNV_XIVE(xrtr);
    uint8_t  blk = SRCNO_BLOCK(srcno);
    uint32_t idx = SRCNO_INDEX(srcno);
    uint64_t eas_addr;

    /* TODO: check when remote EAS lookups are possible */
    if (pnv_xive_get_ic(xive, blk) != xive) {
        xive_error(xive, "VST: EAS %x is remote !?", srcno);
        return -1;
    }

    eas_addr = pnv_xive_vst_addr(xive, VST_TSEL_IVT, blk, idx);
    if (!eas_addr) {
        return -1;
    }

    eas->w &= ~EAS_VALID;
    *((uint64_t *) eas) = ldq_be_dma(&address_space_memory, eas_addr);
    return 0;
}

static int pnv_xive_set_eas(XiveRouter *xrtr, uint32_t srcno, XiveEAS *ive)
{
    /* All done. */
    return 0;
}

static int pnv_xive_eas_update(PnvXive *xive, uint32_t idx)
{
    /* All done. */
    return 0;
}

/*
 * XIVE Set Translation Table configuration
 *
 * The Virtualization Controller MMIO region containing the IPI ESB
 * pages and END ESB pages is sub-divided into "sets" which map
 * portions of the VC region to the different ESB pages. It is
 * configured at runtime through the EDT set translation table to let
 * the firmware decide how to split the address space between IPI ESB
 * pages and END ESB pages.
 */
static int pnv_xive_set_xlate_update(PnvXive *xive, uint64_t val)
{
    uint8_t index = xive->set_xlate_autoinc ?
        xive->set_xlate_index++ : xive->set_xlate_index;
    uint8_t max_index;
    uint64_t *xlate_table;

    switch (xive->set_xlate) {
    case CQ_TAR_TSEL_BLK:
        max_index = ARRAY_SIZE(xive->set_xlate_blk);
        xlate_table = xive->set_xlate_blk;
        break;
    case CQ_TAR_TSEL_MIG:
        max_index = ARRAY_SIZE(xive->set_xlate_mig);
        xlate_table = xive->set_xlate_mig;
        break;
    case CQ_TAR_TSEL_EDT:
        max_index = ARRAY_SIZE(xive->set_xlate_edt);
        xlate_table = xive->set_xlate_edt;
        break;
    case CQ_TAR_TSEL_VDT:
        max_index = ARRAY_SIZE(xive->set_xlate_vdt);
        xlate_table = xive->set_xlate_vdt;
        break;
    default:
        xive_error(xive, "xlate: invalid table %d", (int) xive->set_xlate);
        return -1;
    }

    if (index >= max_index) {
        return -1;
    }

    xlate_table[index] = val;
    return 0;
}

static int pnv_xive_set_xlate_select(PnvXive *xive, uint64_t val)
{
    xive->set_xlate_autoinc = val & CQ_TAR_TBL_AUTOINC;
    xive->set_xlate = val & CQ_TAR_TSEL;
    xive->set_xlate_index = GETFIELD(CQ_TAR_TSEL_INDEX, val);

    return 0;
}

/*
 * Computes the overall size of the IPI or the END ESB pages
 */
static uint64_t pnv_xive_set_xlate_edt_size(PnvXive *xive, uint64_t type)
{
    uint64_t edt_size = 1ull << xive->edt_shift;
    uint64_t size = 0;
    int i;

    for (i = 0; i < XIVE_XLATE_EDT_MAX; i++) {
        uint64_t edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->set_xlate_edt[i]);

        if (edt_type == type) {
            size += edt_size;
        }
    }

    return size;
}

/*
 * Maps an offset of the VC region in the IPI or END region using the
 * layout defined by the EDT table
 */
static uint64_t pnv_xive_set_xlate_edt_offset(PnvXive *xive, uint64_t vc_offset,
                                              uint64_t type)
{
    int i;
    uint64_t edt_size = (1ull << xive->edt_shift);
    uint64_t edt_offset = vc_offset;

    for (i = 0; i < XIVE_XLATE_EDT_MAX && (i * edt_size) < vc_offset; i++) {
        uint64_t edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->set_xlate_edt[i]);

        if (edt_type != type) {
            edt_offset -= edt_size;
        }
    }

    return edt_offset;
}

/*
 * IPI and END sources realize routines
 *
 * We use the EDT table to size the internal XiveSource object backing
 * the IPIs and the XiveENDSource object backing the ENDs
 */
static void pnv_xive_source_realize(PnvXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    uint64_t ipi_mmio_size = pnv_xive_set_xlate_edt_size(xive, CQ_TDR_EDT_IPI);

    /* Two pages per IRQ */
    xive->nr_irqs = ipi_mmio_size / (1ull << (xive->vc_shift + 1));

    /*
     * Configure store EOI if required by firwmare (skiboot has
     * removed support recently though)
     */
    if (xive->regs[VC_SBC_CONFIG >> 3] &
        (VC_SBC_CONF_CPLX_CIST | VC_SBC_CONF_CIST_BOTH)) {
        object_property_set_int(OBJECT(xsrc), XIVE_SRC_STORE_EOI, "flags",
                                &error_fatal);
    }

    object_property_set_int(OBJECT(xsrc), xive->nr_irqs, "nr-irqs",
                            &error_fatal);
    object_property_add_const_link(OBJECT(xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(xsrc), sysbus_get_default());

    /* Install the IPI ESB MMIO region in its VC region */
    memory_region_add_subregion(&xive->ipi_mmio, 0, &xsrc->esb_mmio);

    /* Start in a clean state */
    device_reset(DEVICE(&xive->source));
}

static void pnv_xive_end_source_realize(PnvXive *xive, Error **errp)
{
    XiveENDSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;
    uint64_t end_mmio_size = pnv_xive_set_xlate_edt_size(xive, CQ_TDR_EDT_EQ);

    /* Two pages per END: ESn and ESe */
    xive->nr_ends  = end_mmio_size / (1ull << (xive->vc_shift + 1));

    object_property_set_int(OBJECT(end_xsrc), xive->nr_ends, "nr-ends",
                            &error_fatal);
    object_property_add_const_link(OBJECT(end_xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(end_xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(end_xsrc), sysbus_get_default());

    /* Install the END ESB MMIO region in its VC region */
    memory_region_add_subregion(&xive->end_mmio, 0, &end_xsrc->esb_mmio);
}

/*
 * Virtual Structure Tables (VST) configuration
 */
static void pnv_xive_table_set_exclusive(PnvXive *xive, uint8_t type,
                                         uint8_t blk, uint64_t vsd)
{
    bool gconf_indirect =
        xive->regs[VC_GLOBAL_CONFIG >> 3] & VC_GCONF_INDIRECT;
    uint32_t vst_shift = GETFIELD(VSD_TSIZE, vsd) + 12;
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;

    if (VSD_INDIRECT & vsd) {
        if (!gconf_indirect) {
            xive_error(xive, "VST: %s indirect tables not enabled",
                       vst_infos[type].name);
            return;
        }
    }

    switch (type) {
    case VST_TSEL_IVT:
        /*
         * This is our trigger to create the XiveSource object backing
         * the IPIs.
         */
        pnv_xive_source_realize(xive, &error_fatal);
        break;

    case VST_TSEL_EQDT:
        /* Same trigger but for the XiveENDSource object backing the ENDs. */
        pnv_xive_end_source_realize(xive, &error_fatal);
        break;

    case VST_TSEL_VPDT:
        /* FIXME (skiboot) : remove DD1 workaround on the NVT table size */
        vst_shift = 16;
        break;

    case VST_TSEL_SBE: /* Not modeled */
        /*
         * Contains the backing store pages for the source PQ bits.
         * The XiveSource object has its own. We would need a custom
         * source object to use this backing.
         */
        break;

    case VST_TSEL_IRQ: /* VC only. Not modeled */
        /*
         * These tables contains the backing store pages for the
         * interrupt fifos of the VC sub-engine in case of overflow.
         */
        break;
    default:
        g_assert_not_reached();
    }

    if (!QEMU_IS_ALIGNED(vst_addr, 1ull << vst_shift)) {
        xive_error(xive, "VST: %s table address 0x%"PRIx64" is not aligned with"
                   " page shift %d", vst_infos[type].name, vst_addr, vst_shift);
    }

    /* Keep the VSD for later use */
    xive->vsds[type][blk] = vsd;
}

/*
 * Both PC and VC sub-engines are configured as each use the Virtual
 * Structure Tables : SBE, EAS, END and NVT.
 */
static void pnv_xive_table_set_data(PnvXive *xive, uint64_t vsd, bool pc_engine)
{
    uint8_t mode = GETFIELD(VSD_MODE, vsd);
    uint8_t type = GETFIELD(VST_TABLE_SELECT,
                            xive->regs[VC_VSD_TABLE_ADDR >> 3]);
    uint8_t blk = GETFIELD(VST_TABLE_BLOCK,
                             xive->regs[VC_VSD_TABLE_ADDR >> 3]);
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;

    if (type > VST_TSEL_IRQ) {
        xive_error(xive, "VST: invalid table type %d", type);
        return;
    }

    if (blk >= vst_infos[type].max_blocks) {
        xive_error(xive, "VST: invalid block id %d for"
                      " %s table", blk, vst_infos[type].name);
        return;
    }

    /*
     * Only take the VC sub-engine configuration into account because
     * the XiveRouter model combines both VC and PC sub-engines
     */
    if (pc_engine) {
        return;
    }

    if (!vst_addr) {
        xive_error(xive, "VST: invalid %s table address", vst_infos[type].name);
        return;
    }

    switch (mode) {
    case VSD_MODE_FORWARD:
        xive->vsds[type][blk] = vsd;
        break;

    case VSD_MODE_EXCLUSIVE:
        pnv_xive_table_set_exclusive(xive, type, blk, vsd);
        break;

    default:
        xive_error(xive, "VST: unsupported table mode %d", mode);
        return;
    }
}

/*
 * When the TIMA is accessed from the indirect page, the thread id
 * (PIR) has to be configured in the IC before. This is used for
 * resets and for debug purpose also.
 */
static void pnv_xive_thread_indirect_set(PnvXive *xive, uint64_t val)
{
    int pir = GETFIELD(PC_TCTXT_INDIR_THRDID, xive->regs[PC_TCTXT_INDIR0 >> 3]);

    if (val & PC_TCTXT_INDIR_VALID) {
        if (xive->cpu_ind) {
            xive_error(xive, "IC: indirect access already set for "
                       "invalid PIR %d", pir);
        }

        pir = GETFIELD(PC_TCTXT_INDIR_THRDID, val) & 0xff;
        xive->cpu_ind = ppc_get_vcpu_by_pir(pir);
        if (!xive->cpu_ind) {
            xive_error(xive, "IC: invalid PIR %d for indirect access", pir);
        }
    } else {
        xive->cpu_ind = NULL;
    }
}

/*
 * Interrupt Controller registers MMIO
 */
static void pnv_xive_ic_reg_write(PnvXive *xive, uint32_t offset, uint64_t val,
                                  bool mmio)
{
    MemoryRegion *sysmem = get_system_memory();
    uint32_t reg = offset >> 3;

    switch (offset) {

    /*
     * XIVE CQ (PowerBus bridge) settings
     */
    case CQ_MSGSND:     /* msgsnd for doorbells */
    case CQ_FIRMASK_OR: /* FIR error reporting */
        xive->regs[reg] = val;
        break;
    case CQ_PBI_CTL:
        if (val & CQ_PBI_PC_64K) {
            xive->pc_shift = 16;
        }
        if (val & CQ_PBI_VC_64K) {
            xive->vc_shift = 16;
        }
        break;
    case CQ_CFG_PB_GEN: /* PowerBus General Configuration */
        /*
         * TODO: CQ_INT_ADDR_OPT for 1-block-per-chip mode
         */
        xive->regs[reg] = val;
        break;

    /*
     * XIVE Virtualization Controller settings
     */
    case VC_GLOBAL_CONFIG:
        xive->regs[reg] = val;
        break;

    /*
     * XIVE Presenter Controller settings
     */
    case PC_GLOBAL_CONFIG:
        /* Overrides Int command Chip ID with the Chip ID field */
        if (val & PC_GCONF_CHIPID_OVR) {
            xive->chip_id = GETFIELD(PC_GCONF_CHIPID, val);
        }
        xive->regs[reg] = val;
        break;
    case PC_TCTXT_CFG:
        /*
         * TODO: PC_TCTXT_CFG_BLKGRP_EN for block group support
         * TODO: PC_TCTXT_CFG_HARD_CHIPID_BLK
         */

        /*
         * Moves the chipid into block field for hardwired CAM
         * compares Block offset value is adjusted to 0b0..01 & ThrdId
         */
        if (val & PC_TCTXT_CHIPID_OVERRIDE) {
            xive->thread_chip_id = GETFIELD(PC_TCTXT_CHIPID, val);
        }
        break;
    case PC_TCTXT_TRACK: /* Enable block tracking (DD2) */
        xive->regs[reg] = val;
        break;

    /*
     * Misc settings
     */
    case VC_EQC_CONFIG: /* enable silent escalation */
    case VC_SBC_CONFIG: /* Store EOI configuration */
    case VC_AIB_TX_ORDER_TAG2:
        xive->regs[reg] = val;
        break;

    /*
     * XIVE BAR settings (XSCOM only)
     */
    case CQ_RST_CTL:
        /* resets all bars */
        break;

    case CQ_IC_BAR: /* IC BAR. 8 pages */
        xive->ic_shift = val & CQ_IC_BAR_64K ? 16 : 12;
        if (!(val & CQ_IC_BAR_VALID)) {
            xive->ic_base = 0;
            if (xive->regs[reg] & CQ_IC_BAR_VALID) {
                memory_region_del_subregion(&xive->ic_mmio,
                                            &xive->ic_reg_mmio);
                memory_region_del_subregion(&xive->ic_mmio,
                                            &xive->ic_notify_mmio);
                memory_region_del_subregion(sysmem, &xive->ic_mmio);
                memory_region_del_subregion(sysmem, &xive->tm_mmio_indirect);
            }
        } else {
            xive->ic_base  = val & ~(CQ_IC_BAR_VALID | CQ_IC_BAR_64K);
            if (!(xive->regs[reg] & CQ_IC_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->ic_base,
                                            &xive->ic_mmio);
                memory_region_add_subregion(&xive->ic_mmio,  0,
                                            &xive->ic_reg_mmio);
                memory_region_add_subregion(&xive->ic_mmio,
                                            1ul << xive->ic_shift,
                                            &xive->ic_notify_mmio);
                memory_region_add_subregion(sysmem,
                                   xive->ic_base + (4ull << xive->ic_shift),
                                   &xive->tm_mmio_indirect);
            }
        }
        xive->regs[reg] = val;
        break;

    case CQ_TM1_BAR: /* TM BAR and page size. 4 pages */
    case CQ_TM2_BAR: /* second TM BAR is for hotplug use */
        xive->tm_shift = val & CQ_TM_BAR_64K ? 16 : 12;
        if (!(val & CQ_TM_BAR_VALID)) {
            xive->tm_base = 0;
            if (xive->regs[reg] & CQ_TM_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->tm_mmio);
            }
        } else {
            xive->tm_base  = val & ~(CQ_TM_BAR_VALID | CQ_TM_BAR_64K);
            if (!(xive->regs[reg] & CQ_TM_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->tm_base,
                                            &xive->tm_mmio);
            }
        }
        xive->regs[reg] = val;
       break;

    case CQ_PC_BAR:
        if (!(val & CQ_PC_BAR_VALID)) {
            xive->pc_base = 0;
            if (xive->regs[reg] & CQ_PC_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->pc_mmio);
            }
        } else {
            xive->pc_base = val & ~(CQ_PC_BAR_VALID);
            if (!(xive->regs[reg] & CQ_PC_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->pc_base,
                                            &xive->pc_mmio);
            }
        }
        xive->regs[reg] = val;
        break;
    case CQ_PC_BARM: /* TODO: configure PC BAR size at runtime */
        xive->pc_size =  (~val + 1) & CQ_PC_BARM_MASK;
        xive->regs[reg] = val;

        /* Compute the size of the VDT sets */
        xive->vdt_shift = ctz64(xive->pc_size / XIVE_XLATE_VDT_MAX);
        break;

    case CQ_VC_BAR: /* From 64M to 4TB */
        if (!(val & CQ_VC_BAR_VALID)) {
            xive->vc_base = 0;
            if (xive->regs[reg] & CQ_VC_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->vc_mmio);
            }
        } else {
            xive->vc_base = val & ~(CQ_VC_BAR_VALID);
            if (!(xive->regs[reg] & CQ_VC_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->vc_base,
                                            &xive->vc_mmio);
            }
        }
        xive->regs[reg] = val;
        break;
    case CQ_VC_BARM: /* TODO: configure VC BAR size at runtime */
        xive->vc_size = (~val + 1) & CQ_VC_BARM_MASK;
        xive->regs[reg] = val;

        /* Compute the size of the EDT sets */
        xive->edt_shift = ctz64(xive->vc_size / XIVE_XLATE_EDT_MAX);
        break;

    /*
     * XIVE Set Translation Table settings. Defines the layout of the
     * VC BAR containing the ESB pages of the IPIs and of the ENDs
     */
    case CQ_TAR: /* Set Translation Table Address */
        pnv_xive_set_xlate_select(xive, val);
        break;
    case CQ_TDR: /* Set Translation Table Data */
        pnv_xive_set_xlate_update(xive, val);
        break;

    /*
     * XIVE VC & PC Virtual Structure Table settings
     */
    case VC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_ADDR: /* Virtual table selector */
        xive->regs[reg] = val;
        break;
    case VC_VSD_TABLE_DATA: /* Virtual table setting */
    case PC_VSD_TABLE_DATA:
        pnv_xive_table_set_data(xive, val, offset == PC_VSD_TABLE_DATA);
        break;

    /*
     * Interrupt fifo overflow in memory backing store. Not modeled
     */
    case VC_IRQ_CONFIG_IPI:
    case VC_IRQ_CONFIG_HW:
    case VC_IRQ_CONFIG_CASCADE1:
    case VC_IRQ_CONFIG_CASCADE2:
    case VC_IRQ_CONFIG_REDIST:
    case VC_IRQ_CONFIG_IPI_CASC:
        xive->regs[reg] = val;
        break;

    /*
     * XIVE hardware thread enablement
     */
    case PC_THREAD_EN_REG0_SET: /* Physical Thread Enable */
    case PC_THREAD_EN_REG1_SET: /* Physical Thread Enable (fused core) */
        xive->regs[reg] |= val;
        break;
    case PC_THREAD_EN_REG0_CLR:
        xive->regs[PC_THREAD_EN_REG0_SET >> 3] &= ~val;
        break;
    case PC_THREAD_EN_REG1_CLR:
        xive->regs[PC_THREAD_EN_REG1_SET >> 3] &= ~val;
        break;

    /*
     * Indirect TIMA access set up. Defines the HW thread to use.
     */
    case PC_TCTXT_INDIR0:
        pnv_xive_thread_indirect_set(xive, val);
        xive->regs[reg] = val;
        break;
    case PC_TCTXT_INDIR1:
    case PC_TCTXT_INDIR2:
    case PC_TCTXT_INDIR3:
        /* TODO: check what PC_TCTXT_INDIR[123] are for */
        xive->regs[reg] = val;
        break;

    /*
     * XIVE PC & VC cache updates for EAS, NVT and END
     */
    case PC_VPC_SCRUB_MASK:
    case PC_VPC_CWATCH_SPEC:
    case VC_EQC_SCRUB_MASK:
    case VC_EQC_CWATCH_SPEC:
    case VC_IVC_SCRUB_MASK:
        xive->regs[reg] = val;
        break;
    case VC_IVC_SCRUB_TRIG:
        pnv_xive_eas_update(xive, GETFIELD(VC_SCRUB_OFFSET, val));
        break;
    case PC_VPC_CWATCH_DAT0:
    case PC_VPC_CWATCH_DAT1:
    case PC_VPC_CWATCH_DAT2:
    case PC_VPC_CWATCH_DAT3:
    case PC_VPC_CWATCH_DAT4:
    case PC_VPC_CWATCH_DAT5:
    case PC_VPC_CWATCH_DAT6:
    case PC_VPC_CWATCH_DAT7:
        xive->vpc_watch[(offset - PC_VPC_CWATCH_DAT0) / 8] = cpu_to_be64(val);
        break;
    case PC_VPC_SCRUB_TRIG:
        pnv_xive_nvt_update(xive, GETFIELD(PC_SCRUB_BLOCK_ID, val),
                           GETFIELD(PC_SCRUB_OFFSET, val));
        break;
    case VC_EQC_CWATCH_DAT0:
    case VC_EQC_CWATCH_DAT1:
    case VC_EQC_CWATCH_DAT2:
    case VC_EQC_CWATCH_DAT3:
        xive->eqc_watch[(offset - VC_EQC_CWATCH_DAT0) / 8] = cpu_to_be64(val);
        break;
    case VC_EQC_SCRUB_TRIG:
        pnv_xive_end_update(xive, GETFIELD(VC_SCRUB_BLOCK_ID, val),
                            GETFIELD(VC_SCRUB_OFFSET, val));
        break;

    /*
     * XIVE PC & VC cache invalidation
     */
    case PC_AT_KILL:
        xive->regs[reg] |= val;
        break;
    case VC_AT_MACRO_KILL:
        xive->regs[reg] |= val;
        break;
    case PC_AT_KILL_MASK:
    case VC_AT_MACRO_KILL_MASK:
        xive->regs[reg] = val;
        break;

    default:
        xive_error(xive, "IC: invalid write to reg=0x%08x mmio=%d", offset,
                   mmio);
    }
}

static uint64_t pnv_xive_ic_reg_read(PnvXive *xive, uint32_t offset, bool mmio)
{
    uint64_t val = 0;
    uint32_t reg = offset >> 3;

    switch (offset) {
    case CQ_CFG_PB_GEN:
    case CQ_IC_BAR:
    case CQ_TM1_BAR:
    case CQ_TM2_BAR:
    case CQ_PC_BAR:
    case CQ_PC_BARM:
    case CQ_VC_BAR:
    case CQ_VC_BARM:
    case CQ_TAR:
    case CQ_TDR:
    case CQ_PBI_CTL:

    case PC_TCTXT_CFG:
    case PC_TCTXT_TRACK:
    case PC_TCTXT_INDIR0:
    case PC_TCTXT_INDIR1:
    case PC_TCTXT_INDIR2:
    case PC_TCTXT_INDIR3:
    case PC_GLOBAL_CONFIG:

    case PC_VPC_SCRUB_MASK:
    case PC_VPC_CWATCH_SPEC:
    case PC_VPC_CWATCH_DAT0:
    case PC_VPC_CWATCH_DAT1:
    case PC_VPC_CWATCH_DAT2:
    case PC_VPC_CWATCH_DAT3:
    case PC_VPC_CWATCH_DAT4:
    case PC_VPC_CWATCH_DAT5:
    case PC_VPC_CWATCH_DAT6:
    case PC_VPC_CWATCH_DAT7:

    case VC_GLOBAL_CONFIG:
    case VC_AIB_TX_ORDER_TAG2:

    case VC_IRQ_CONFIG_IPI:
    case VC_IRQ_CONFIG_HW:
    case VC_IRQ_CONFIG_CASCADE1:
    case VC_IRQ_CONFIG_CASCADE2:
    case VC_IRQ_CONFIG_REDIST:
    case VC_IRQ_CONFIG_IPI_CASC:

    case VC_EQC_SCRUB_MASK:
    case VC_EQC_CWATCH_DAT0:
    case VC_EQC_CWATCH_DAT1:
    case VC_EQC_CWATCH_DAT2:
    case VC_EQC_CWATCH_DAT3:

    case VC_EQC_CWATCH_SPEC:
    case VC_IVC_SCRUB_MASK:
    case VC_SBC_CONFIG:
    case VC_AT_MACRO_KILL_MASK:
    case VC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_ADDR:
    case VC_VSD_TABLE_DATA:
    case PC_VSD_TABLE_DATA:
        val = xive->regs[reg];
        break;

    case CQ_MSGSND: /* Identifies which cores have msgsnd enabled.
                     * Say all have. */
        val = 0xffffff0000000000;
        break;

    /*
     * XIVE PC & VC cache updates for EAS, NVT and END
     */
    case PC_VPC_SCRUB_TRIG:
    case VC_IVC_SCRUB_TRIG:
    case VC_EQC_SCRUB_TRIG:
        xive->regs[reg] &= ~VC_SCRUB_VALID;
        val = xive->regs[reg];
        break;

    /*
     * XIVE PC & VC cache invalidation
     */
    case PC_AT_KILL:
        xive->regs[reg] &= ~PC_AT_KILL_VALID;
        val = xive->regs[reg];
        break;
    case VC_AT_MACRO_KILL:
        xive->regs[reg] &= ~VC_KILL_VALID;
        val = xive->regs[reg];
        break;

    /*
     * XIVE synchronisation
     */
    case VC_EQC_CONFIG:
        val = VC_EQC_SYNC_MASK;
        break;

    default:
        xive_error(xive, "IC: invalid read reg=0x%08x mmio=%d", offset, mmio);
    }

    return val;
}

static void pnv_xive_ic_reg_write_mmio(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    pnv_xive_ic_reg_write(opaque, addr, val, true);
}

static uint64_t pnv_xive_ic_reg_read_mmio(void *opaque, hwaddr addr,
                                      unsigned size)
{
    return pnv_xive_ic_reg_read(opaque, addr, true);
}

static const MemoryRegionOps pnv_xive_ic_reg_ops = {
    .read = pnv_xive_ic_reg_read_mmio,
    .write = pnv_xive_ic_reg_write_mmio,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * Interrupt Controller MMIO: Notify port page (write only)
 */
#define PNV_XIVE_FORWARD_IPI        0x800 /* Forward IPI */
#define PNV_XIVE_FORWARD_HW         0x880 /* Forward HW */
#define PNV_XIVE_FORWARD_OS_ESC     0x900 /* Forward OS escalation */
#define PNV_XIVE_FORWARD_HW_ESC     0x980 /* Forward Hyp escalation */
#define PNV_XIVE_FORWARD_REDIS      0xa00 /* Forward Redistribution */
#define PNV_XIVE_RESERVED5          0xa80 /* Cache line 5 PowerBUS operation */
#define PNV_XIVE_RESERVED6          0xb00 /* Cache line 6 PowerBUS operation */
#define PNV_XIVE_RESERVED7          0xb80 /* Cache line 7 PowerBUS operation */

/* VC synchronisation */
#define PNV_XIVE_SYNC_IPI           0xc00 /* Sync IPI */
#define PNV_XIVE_SYNC_HW            0xc80 /* Sync HW */
#define PNV_XIVE_SYNC_OS_ESC        0xd00 /* Sync OS escalation */
#define PNV_XIVE_SYNC_HW_ESC        0xd80 /* Sync Hyp escalation */
#define PNV_XIVE_SYNC_REDIS         0xe00 /* Sync Redistribution */

/* PC synchronisation */
#define PNV_XIVE_SYNC_PULL          0xe80 /* Sync pull context */
#define PNV_XIVE_SYNC_PUSH          0xf00 /* Sync push context */
#define PNV_XIVE_SYNC_VPC           0xf80 /* Sync remove VPC store */

static void pnv_xive_ic_hw_trigger(PnvXive *xive, hwaddr addr, uint64_t val)
{
    XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xive);

    xfc->notify(XIVE_FABRIC(xive), val);
}

static void pnv_xive_ic_notify_write(void *opaque, hwaddr addr, uint64_t val,
                                     unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    /* VC: HW triggers */
    switch (addr) {
    case 0x000 ... 0x7FF:
        pnv_xive_ic_hw_trigger(opaque, addr, val);
        break;

    /* VC: Forwarded IRQs */
    case PNV_XIVE_FORWARD_IPI:
    case PNV_XIVE_FORWARD_HW:
    case PNV_XIVE_FORWARD_OS_ESC:
    case PNV_XIVE_FORWARD_HW_ESC:
    case PNV_XIVE_FORWARD_REDIS:
        /* TODO: forwarded IRQs. Should be like HW triggers */
        xive_error(xive, "IC: forwarded at @0x%"HWADDR_PRIx" IRQ 0x%"PRIx64,
                   addr, val);
        break;

    /* VC syncs */
    case PNV_XIVE_SYNC_IPI:
    case PNV_XIVE_SYNC_HW:
    case PNV_XIVE_SYNC_OS_ESC:
    case PNV_XIVE_SYNC_HW_ESC:
    case PNV_XIVE_SYNC_REDIS:
        break;

    /* PC sync */
    case PNV_XIVE_SYNC_PULL:
    case PNV_XIVE_SYNC_PUSH:
    case PNV_XIVE_SYNC_VPC:
        break;

    default:
        xive_error(xive, "IC: invalid notify write @%"HWADDR_PRIx, addr);
    }
}

static uint64_t pnv_xive_ic_notify_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    /* loads are invalid */
    xive_error(xive, "IC: invalid notify read @%"HWADDR_PRIx, addr);
    return -1;
}

static const MemoryRegionOps pnv_xive_ic_notify_ops = {
    .read = pnv_xive_ic_notify_read,
    .write = pnv_xive_ic_notify_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * Interrupt controller MMIO region. The layout is compatible between
 * 4K and 64K pages :
 *
 * Page 0           sub-engine BARs
 *  0x000 - 0x3FF   IC registers
 *  0x400 - 0x7FF   PC registers
 *  0x800 - 0xFFF   VC registers
 *
 * Page 1           Notify page
 *  0x000 - 0x7FF   HW interrupt triggers (PSI, PHB)
 *  0x800 - 0xFFF   forwards and syncs
 *
 * Page 2           LSI Trigger page (writes only) (not modeled)
 * Page 3           LSI SB EOI page (reads only) (not modeled)
 *
 * Page 4-7         indirect TIMA (aliased to TIMA region)
 */
static void pnv_xive_ic_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "IC: invalid write @%"HWADDR_PRIx, addr);
}

static uint64_t pnv_xive_ic_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "IC: invalid read @%"HWADDR_PRIx, addr);
    return -1;
}

static const MemoryRegionOps pnv_xive_ic_ops = {
    .read = pnv_xive_ic_read,
    .write = pnv_xive_ic_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * Interrupt controller XSCOM region. Load accesses are nearly all
 * done all through the MMIO region.
 */
static uint64_t pnv_xive_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    switch (addr >> 3) {
    case X_VC_EQC_CONFIG:
        /*
         * This is the only XSCOM load done in skiboot. Bizarre. To be
         * checked.
         */
        return VC_EQC_SYNC_MASK;
    default:
        return pnv_xive_ic_reg_read(xive, addr, false);
    }
}

static void pnv_xive_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    pnv_xive_ic_reg_write(opaque, addr, val, false);
}

static const MemoryRegionOps pnv_xive_xscom_ops = {
    .read = pnv_xive_xscom_read,
    .write = pnv_xive_xscom_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    }
};

/*
 * Virtualization Controller MMIO region containing the IPI and END ESB pages
 */
static uint64_t pnv_xive_vc_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    uint64_t edt_index = offset >> xive->edt_shift;
    uint64_t edt_type = 0;
    uint64_t ret = -1;
    uint64_t edt_offset;
    MemTxResult result;
    AddressSpace *edt_as = NULL;

    if (edt_index < XIVE_XLATE_EDT_MAX) {
        edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->set_xlate_edt[edt_index]);
    }

    switch (edt_type) {
    case CQ_TDR_EDT_IPI:
        edt_as = &xive->ipi_as;
        break;
    case CQ_TDR_EDT_EQ:
        edt_as = &xive->end_as;
        break;
    default:
        xive_error(xive, "VC: invalid read @%"HWADDR_PRIx, offset);
        return -1;
    }

    /* remap the offset for the targeted address space */
    edt_offset = pnv_xive_set_xlate_edt_offset(xive, offset, edt_type);

    ret = address_space_ldq(edt_as, edt_offset, MEMTXATTRS_UNSPECIFIED,
                            &result);
    if (result != MEMTX_OK) {
        xive_error(xive, "VC: %s read failed at @0x%"HWADDR_PRIx " -> @0x%"
                   HWADDR_PRIx, edt_type == CQ_TDR_EDT_IPI ? "IPI" : "END",
                   offset, edt_offset);
        return -1;
    }

    return ret;
}

static void pnv_xive_vc_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    uint64_t edt_index = offset >> xive->edt_shift;
    uint64_t edt_type = 0;
    uint64_t edt_offset;
    MemTxResult result;
    AddressSpace *edt_as = NULL;

    if (edt_index < XIVE_XLATE_EDT_MAX) {
        edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->set_xlate_edt[edt_index]);
    }

    switch (edt_type) {
    case CQ_TDR_EDT_IPI:
        edt_as = &xive->ipi_as;
        break;
    case CQ_TDR_EDT_EQ:
        edt_as = &xive->end_as;
        break;
    default:
        xive_error(xive, "VC: invalid read @%"HWADDR_PRIx, offset);
        return;
    }

    /* remap the offset for the targeted address space */
    edt_offset = pnv_xive_set_xlate_edt_offset(xive, offset, edt_type);

    address_space_stq(edt_as, edt_offset, val, MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        xive_error(xive, "VC: write failed at @0x%"HWADDR_PRIx, edt_offset);
    }
}

static const MemoryRegionOps pnv_xive_vc_ops = {
    .read = pnv_xive_vc_read,
    .write = pnv_xive_vc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * Presenter Controller MMIO region. This is used by the Virtualization
 * Controller to update the IPB in the NVT table when required. Not
 * implemented.
 */
static uint64_t pnv_xive_pc_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "PC: invalid read @%"HWADDR_PRIx, addr);
    return -1;
}

static void pnv_xive_pc_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "PC: invalid write to VC @%"HWADDR_PRIx, addr);
}

static const MemoryRegionOps pnv_xive_pc_ops = {
    .read = pnv_xive_pc_read,
    .write = pnv_xive_pc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

void pnv_xive_pic_print_info(PnvXive *xive, Monitor *mon)
{
    XiveRouter *xrtr = XIVE_ROUTER(xive);
    XiveEAS eas;
    XiveEND end;
    uint32_t endno = 0;
    uint32_t srcno0 = XIVE_SRCNO(xive->chip_id, 0);
    uint32_t srcno = srcno0;

    monitor_printf(mon, "XIVE[%x] Source %08x .. %08x\n", xive->chip_id,
                  srcno0, srcno0 + xive->source.nr_irqs - 1);
    xive_source_pic_print_info(&xive->source, srcno0, mon);

    monitor_printf(mon, "XIVE[%x] EAT %08x .. %08x\n", xive->chip_id,
                   srcno0, srcno0 + xive->nr_irqs - 1);
    while (!xive_router_get_eas(xrtr, srcno, &eas)) {
        if (!(eas.w & EAS_MASKED)) {
            xive_eas_pic_print_info(&eas, srcno, mon);
        }
        srcno++;
    }

    monitor_printf(mon, "XIVE[%x] ENDT %08x .. %08x\n", xive->chip_id,
                   0, xive->nr_ends - 1);
    while (!xive_router_get_end(xrtr, xrtr->chip_id, endno, &end)) {
        xive_end_pic_print_info(&end, endno++, mon);
    }
}

static void pnv_xive_reset(DeviceState *dev)
{
    PnvXive *xive = PNV_XIVE(dev);
    PnvChip *chip = PNV_CHIP(object_property_get_link(OBJECT(dev), "chip",
                                                      &error_fatal));

    /*
     * Use the chip id to identify the XIVE interrupt controller. It
     * can be overriden by configuration at runtime.
     */
    xive->chip_id = xive->thread_chip_id = chip->chip_id;

    /* Default page size. Should be changed at runtime to 64k */
    xive->ic_shift = xive->vc_shift = xive->pc_shift = 12;

    /*
     * PowerNV XIVE sources are realized at runtime when the set
     * translation tables are configured.
     */
    if (DEVICE(&xive->source)->realized) {
        object_property_set_bool(OBJECT(&xive->source), false, "realized",
                                 &error_fatal);
    }

    if (DEVICE(&xive->end_source)->realized) {
        object_property_set_bool(OBJECT(&xive->end_source), false, "realized",
                                 &error_fatal);
    }
}

/*
 * The VC sub-engine incorporates a source controller for the IPIs.
 * When triggered, we need to construct a source number with the
 * chip/block identifier
 */
static void pnv_xive_notify(XiveFabric *xf, uint32_t srcno)
{
    PnvXive *xive = PNV_XIVE(xf);

    xive_router_notify(xf, XIVE_SRCNO(xive->chip_id, srcno));
}

static void pnv_xive_init(Object *obj)
{
    PnvXive *xive = PNV_XIVE(obj);

    object_initialize(&xive->source, sizeof(xive->source), TYPE_XIVE_SOURCE);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);

    object_initialize(&xive->end_source, sizeof(xive->end_source),
                      TYPE_XIVE_END_SOURCE);
    object_property_add_child(obj, "end_source", OBJECT(&xive->end_source),
                              NULL);
}

static void pnv_xive_realize(DeviceState *dev, Error **errp)
{
    PnvXive *xive = PNV_XIVE(dev);

    /* Default page size. Generally changed at runtime to 64k */
    xive->ic_shift = xive->vc_shift = xive->pc_shift = 12;

    /* XSCOM region, used for initial configuration of the BARs */
    memory_region_init_io(&xive->xscom_regs, OBJECT(dev), &pnv_xive_xscom_ops,
                          xive, "xscom-xive", PNV9_XSCOM_XIVE_SIZE << 3);

    /* Interrupt controller MMIO region */
    memory_region_init_io(&xive->ic_mmio, OBJECT(dev), &pnv_xive_ic_ops, xive,
                          "xive.ic", PNV9_XIVE_IC_SIZE);
    memory_region_init_io(&xive->ic_reg_mmio, OBJECT(dev), &pnv_xive_ic_reg_ops,
                          xive, "xive.ic.reg", 1 << xive->ic_shift);
    memory_region_init_io(&xive->ic_notify_mmio, OBJECT(dev),
                          &pnv_xive_ic_notify_ops,
                          xive, "xive.ic.notify", 1 << xive->ic_shift);

    /* The Pervasive LSI trigger and EOI pages are not modeled */

    /*
     * Overall Virtualization Controller MMIO region containing the
     * IPI ESB pages and END ESB pages. The layout is defined by the
     * EDT set translation table and the accesses are dispatched using
     * address spaces for each.
     */
    memory_region_init_io(&xive->vc_mmio, OBJECT(xive), &pnv_xive_vc_ops, xive,
                          "xive.vc", PNV9_XIVE_VC_SIZE);

    memory_region_init(&xive->ipi_mmio, OBJECT(xive), "xive.vc.ipi",
                       PNV9_XIVE_VC_SIZE);
    address_space_init(&xive->ipi_as, &xive->ipi_mmio, "xive.vc.ipi");
    memory_region_init(&xive->end_mmio, OBJECT(xive), "xive.vc.end",
                       PNV9_XIVE_VC_SIZE);
    address_space_init(&xive->end_as, &xive->end_mmio, "xive.vc.end");


    /* Presenter Controller MMIO region (not implemented) */
    memory_region_init_io(&xive->pc_mmio, OBJECT(xive), &pnv_xive_pc_ops, xive,
                          "xive.pc", PNV9_XIVE_PC_SIZE);

    /* Thread Interrupt Management Area, direct an indirect */
    memory_region_init_io(&xive->tm_mmio, OBJECT(xive), &xive_tm_ops,
                          &xive->cpu_ind, "xive.tima", PNV9_XIVE_TM_SIZE);
    memory_region_init_alias(&xive->tm_mmio_indirect, OBJECT(xive),
                             "xive.tima.indirect",
                             &xive->tm_mmio, 0, PNV9_XIVE_TM_SIZE);
}

static int pnv_xive_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power9-xive-x";
    char *name;
    int offset;
    uint32_t lpc_pcba = PNV9_XSCOM_XIVE_BASE;
    uint32_t reg[] = {
        cpu_to_be32(lpc_pcba),
        cpu_to_be32(PNV9_XSCOM_XIVE_SIZE)
    };

    name = g_strdup_printf("xive@%x", lpc_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat,
                      sizeof(compat))));
    return 0;
}

static Property pnv_xive_properties[] = {
    DEFINE_PROP_UINT64("ic-bar", PnvXive, ic_base, 0),
    DEFINE_PROP_UINT64("vc-bar", PnvXive, vc_base, 0),
    DEFINE_PROP_UINT64("pc-bar", PnvXive, pc_base, 0),
    DEFINE_PROP_UINT64("tm-bar", PnvXive, tm_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);
    XiveRouterClass *xrc = XIVE_ROUTER_CLASS(klass);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(klass);

    xdc->dt_xscom = pnv_xive_dt_xscom;

    dc->desc = "PowerNV XIVE Interrupt Controller";
    dc->realize = pnv_xive_realize;
    dc->props = pnv_xive_properties;
    dc->reset = pnv_xive_reset;

    xrc->get_eas = pnv_xive_get_eas;
    xrc->set_eas = pnv_xive_set_eas;
    xrc->get_end = pnv_xive_get_end;
    xrc->set_end = pnv_xive_set_end;
    xrc->get_nvt  = pnv_xive_get_nvt;
    xrc->set_nvt  = pnv_xive_set_nvt;

    xfc->notify  = pnv_xive_notify;
};

static const TypeInfo pnv_xive_info = {
    .name          = TYPE_PNV_XIVE,
    .parent        = TYPE_XIVE_ROUTER,
    .instance_init = pnv_xive_init,
    .instance_size = sizeof(PnvXive),
    .class_init    = pnv_xive_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_xive_register_types(void)
{
    type_register_static(&pnv_xive_info);
}

type_init(pnv_xive_register_types)
