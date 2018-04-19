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

#define EQ_PER_PAGE           (0x10000 / sizeof(XiveEQ))
#define VP_PER_PAGE           (0x10000 / sizeof(XiveVP))

static uint64_t pnv_xive_eq_addr(PnvXive *xive, uint32_t idx)
{
    uint64_t vsd;
    uint64_t page_addr;

    if (idx >= xive->eqdt_count) {
        return 0;
    }

    vsd = be64_to_cpu(xive->eqdt[idx / EQ_PER_PAGE]);
    page_addr = vsd & VSD_ADDRESS_MASK;
    if (!page_addr) {
        return 0;
    }

    /* We don't support nested indirect tables */
    if (VSD_INDIRECT & vsd) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: found a nested indirect EQ"
                      " table at index %d\n", idx);
        return 0;
    }

    return page_addr + (idx % EQ_PER_PAGE) * sizeof(XiveEQ);
}

static int pnv_xive_eq_get(PnvXive *xive, uint32_t idx, XiveEQ *eq)
{
    uint64_t eq_addr = pnv_xive_eq_addr(xive, idx);

    if (!eq_addr) {
        return -1;
    }

    cpu_physical_memory_read(eq_addr, eq, sizeof(XiveEQ));
    eq->w0 = be32_to_cpu(eq->w0);
    eq->w1 = be32_to_cpu(eq->w1);
    eq->w2 = be32_to_cpu(eq->w2);
    eq->w3 = be32_to_cpu(eq->w3);
    eq->w4 = be32_to_cpu(eq->w4);
    eq->w5 = be32_to_cpu(eq->w5);
    eq->w6 = be32_to_cpu(eq->w6);
    eq->w7 = be32_to_cpu(eq->w7);

    return 0;
}

static int pnv_xive_eq_set(PnvXive *xive, uint32_t idx, XiveEQ *in_eq)
{
    XiveEQ eq;
    uint64_t eq_addr = pnv_xive_eq_addr(xive, idx);

    if (!eq_addr) {
        return -1;
    }

    eq.w0 = cpu_to_be32(in_eq->w0);
    eq.w1 = cpu_to_be32(in_eq->w1);
    eq.w2 = cpu_to_be32(in_eq->w2);
    eq.w3 = cpu_to_be32(in_eq->w3);
    eq.w4 = cpu_to_be32(in_eq->w4);
    eq.w5 = cpu_to_be32(in_eq->w5);
    eq.w6 = cpu_to_be32(in_eq->w6);
    eq.w7 = cpu_to_be32(in_eq->w7);
    cpu_physical_memory_write(eq_addr, &eq, sizeof(XiveEQ));
    return 0;
}

static void pnv_xive_eq_update(PnvXive *xive, uint32_t idx)
{
    uint32_t size = 1 << (GETFIELD(VSD_TSIZE, xive->vsds[VST_TSEL_EQDT]) + 12);
    uint64_t eqdt_addr = xive->vsds[VST_TSEL_EQDT] & VSD_ADDRESS_MASK;
    uint64_t eq_addr;

    /* Update the EQ indirect table which might have newly allocated
     * pages. We could use the idx to limit the transfer */
    cpu_physical_memory_read(eqdt_addr, xive->eqdt, size);

    eq_addr = pnv_xive_eq_addr(xive, idx);
    if (!eq_addr) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Update failed for EQ %d\n", idx);
        return;
    }

    cpu_physical_memory_write(eq_addr, xive->eqc_watch, sizeof(XiveEQ));
}

static uint64_t pnv_xive_vp_addr(PnvXive *xive, uint32_t idx)
{
    uint64_t vsd;
    uint64_t page_addr;

    if (idx >= xive->vpdt_count) {
        return 0;
    }

    vsd = be64_to_cpu(xive->vpdt[idx / VP_PER_PAGE]);
    page_addr = vsd & VSD_ADDRESS_MASK;
    if (!page_addr) {
        return 0;
    }

    /* We don't support nested indirect tables */
    if (VSD_INDIRECT & vsd) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: found a nested indirect VP"
                      " table at index %x\n", idx);
        return 0;
    }

    return page_addr + (idx % VP_PER_PAGE) * sizeof(XiveVP);
}

static int pnv_xive_vp_get(PnvXive *xive, uint32_t idx, XiveVP *vp)
{
    uint64_t vp_addr = pnv_xive_vp_addr(xive, idx);

    if (!vp_addr) {
        return -1;
    }

    cpu_physical_memory_read(vp_addr, vp, sizeof(XiveVP));
    vp->w0 = cpu_to_be32(vp->w0);
    vp->w1 = cpu_to_be32(vp->w1);
    vp->w2 = cpu_to_be32(vp->w2);
    vp->w3 = cpu_to_be32(vp->w3);
    vp->w4 = cpu_to_be32(vp->w4);
    vp->w5 = cpu_to_be32(vp->w5);
    vp->w6 = cpu_to_be32(vp->w6);
    vp->w7 = cpu_to_be32(vp->w7);

    return 0;
}

static void pnv_xive_vp_update(PnvXive *xive, uint32_t idx)
{
    uint32_t size = 1 << (GETFIELD(VSD_TSIZE, xive->vsds[VST_TSEL_VPDT]) + 12);
    uint64_t vpdt_addr = xive->vsds[VST_TSEL_VPDT] & VSD_ADDRESS_MASK;
    uint64_t vp_addr;

    /* Update the VP indirect table which might have newly allocated
     * pages. We could use the idx to limit the transfer */
    cpu_physical_memory_read(vpdt_addr, xive->vpdt, size);

    vp_addr = pnv_xive_vp_addr(xive, idx);
    if (!vp_addr) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Update failed for VP %x\n", idx);
        return;
    }

    cpu_physical_memory_write(vp_addr, xive->vpc_watch, sizeof(XiveVP));
}

static void pnv_xive_ive_update(PnvXive *xive, uint32_t idx)
{
    uint64_t ivt_addr = xive->vsds[VST_TSEL_IVT] & VSD_ADDRESS_MASK;
    uint64_t ive_addr = ivt_addr + idx * sizeof(XiveIVE);
    XiveIVE *ive = &xive->ivt[idx];

    *((uint64_t *) ive) = ldq_be_dma(&address_space_memory, ive_addr);
}

#define PNV_XIVE_SET_XLATE_SIZE  (8ull << 30)

static uint64_t pnv_xive_set_xlate_edt_size(PnvXive *xive, uint64_t type)
{
    uint64_t size = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(xive->set_xlate_edt); i++) {
        /* This is supposing that the IPIs and EQs set translations
         * are contiguous */
        uint64_t edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->set_xlate_edt[i]);

        if (edt_type == type) {
            size += PNV_XIVE_SET_XLATE_SIZE;
        }
    }

    return size;
}

static int pnv_xive_set_xlate_update(PnvXive *xive, uint64_t val)
{
    uint8_t index = xive->set_xlate_autoinc ?
        xive->set_xlate_index++ : xive->set_xlate_index;

    switch (xive->set_xlate) {
    case CQ_TAR_TSEL_EDT:
        index %= sizeof(xive->set_xlate_edt);
        xive->set_xlate_edt[index] = val;
        break;
    case CQ_TAR_TSEL_VDT:
        index %= sizeof(xive->set_xlate_vdt);
        xive->set_xlate_vdt[index] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid table set %d\n",
                      (int) xive->set_xlate);
        return -1;
    }

    return 0;
}

static int pnv_xive_set_xlate_select(PnvXive *xive, uint64_t val)
{
    xive->set_xlate_autoinc = val & CQ_TAR_TBL_AUTOINC;
    xive->set_xlate = val & CQ_TAR_TSEL;
    xive->set_xlate_index = GETFIELD(CQ_TAR_TSEL_INDEX, val);

    return 0;
}

static void pnv_xive_source_realize(PnvXive *xive, uint32_t count,
                                    Error **errp)
{
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    uint64_t esb_mmio_size = pnv_xive_set_xlate_edt_size(xive, CQ_TDR_EDT_IPI);

    /* Remap the ESB region for IPIs now that the set translation have
     * been configured.
     */
    memory_region_transaction_begin();
    memory_region_set_size(&xive->esb_mmio, esb_mmio_size);
    memory_region_set_enabled(&xive->esb_mmio, true);
    memory_region_transaction_commit();

    object_property_set_int(OBJECT(xsrc), xive->esb_base, "bar", &error_fatal);
    object_property_set_int(OBJECT(xsrc), XIVE_ESB_64K_2PAGE, "shift",
                            &error_fatal);
    object_property_set_int(OBJECT(xsrc), count, "nr-irqs", &error_fatal);
    object_property_add_const_link(OBJECT(xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(xsrc), sysbus_get_default());

    /* Install the ESB MMIO region in the overall region configured
     * for the purpose in the interrupt controller . */
    memory_region_add_subregion(&xive->esb_mmio, 0, &xsrc->esb_mmio);
}

static void pnv_xive_eq_source_realize(PnvXive *xive, uint32_t count,
                                       Error **errp)
{
    XiveSource *eq_xsrc = &xive->eq_source;
    Error *local_err = NULL;
    uint64_t esb_mmio_size = pnv_xive_set_xlate_edt_size(xive, CQ_TDR_EDT_IPI);
    uint64_t eq_mmio_size = pnv_xive_set_xlate_edt_size(xive, CQ_TDR_EDT_EQ);

    xive->eq_base = xive->vc_base + esb_mmio_size;

    /* Remap the ESB region for EQs now that the set translation have
     * been configured.
     */
    memory_region_transaction_begin();
    memory_region_set_size(&xive->eq_mmio, eq_mmio_size);
    memory_region_set_address(&xive->eq_mmio, esb_mmio_size);
    memory_region_set_enabled(&xive->eq_mmio, true);
    memory_region_transaction_commit();

    /* check for some skiboot oddity on the table size */
    if (xive->eq_base + count * (1ull << XIVE_ESB_64K_2PAGE) >
        xive->vc_base + PNV_XIVE_VC_SIZE) {
        uint32_t old = count;
        count = (xive->vc_base + PNV_XIVE_VC_SIZE -
                 xive->eq_base) >> XIVE_ESB_64K_2PAGE;
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: EQ count %d too large for VC "
                      "MMIO region. shrinking to %d\n", old, count);
    }

    object_property_set_int(OBJECT(eq_xsrc), xive->eq_base, "bar",
                            &error_fatal);
    object_property_set_int(OBJECT(eq_xsrc), XIVE_ESB_64K_2PAGE, "shift",
                            &error_fatal);
    object_property_set_int(OBJECT(eq_xsrc), count, "nr-irqs", &error_fatal);
    object_property_add_const_link(OBJECT(eq_xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(eq_xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(eq_xsrc), sysbus_get_default());

    /* Install the EQ ESB MMIO region in the overall region configured
     * for the purpose in the interrupt controller . */
    memory_region_add_subregion(&xive->eq_mmio, 0, &eq_xsrc->esb_mmio);
}

static void pnv_xive_table_set_data(PnvXive *xive, uint64_t val, bool pc_engine)
{
    uint64_t addr = val & VSD_ADDRESS_MASK;
    uint32_t size = 1 << (GETFIELD(VSD_TSIZE, val) + 12);
    bool indirect = VSD_INDIRECT & val;
    uint8_t mode = GETFIELD(VSD_MODE, val);

    if (mode != VSD_MODE_EXCLUSIVE) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: no support for non-exclusive"
                      " tables");
        return;
    }

    switch (xive->vst_tsel) {
    case VST_TSEL_IVT:
        if (!xive->ivt) {
            xive->nr_irqs = size / sizeof(XiveIVE);

            xive->ivt = g_new0(XiveIVE, xive->nr_irqs);

            /* Read initial state from the guest RAM */
            cpu_physical_memory_read(addr, xive->ivt, size);
            xive->vsds[xive->vst_tsel] = val;
        }
        break;

    case VST_TSEL_SBE:
        /* We do not use the SBE bits backed in the guest RAM but
         * instead, we create our own source. The IVT table should
         * have been created before.
         */
        if (!DEVICE(&xive->source)->realized) {

            pnv_xive_source_realize(xive, xive->nr_irqs, &error_fatal);
            device_reset(DEVICE(&xive->source));
            xive->vsds[xive->vst_tsel] = val;
        }
        break;

    case VST_TSEL_EQDT:
        if (!xive->eqdt) {

            /* EQDT is expected to be indirect even though skiboot can
             * be compiled in direct mode */
            assert(indirect);

            /* FIXME: skiboot set the EQDT as indirect with 64K
             * subpages, which is too big for the VC MMIO region.
             */
            val &= ~VSD_TSIZE;
            val |= SETFIELD(VSD_TSIZE, 0ull, 0);
            size = 0x1000;

            xive->eqdt_count = size * EQ_PER_PAGE / 8;

            xive->eqdt = g_malloc0(size);

            /* Should be all NULL pointers */
            cpu_physical_memory_read(addr, xive->eqdt, size);

            xive->vsds[xive->vst_tsel] = val;

            /* We do not use the ESn bits of the XiveEQ structure
             * backed in the guest RAM but instead, we create our own
             * source.
             */
            pnv_xive_eq_source_realize(xive, xive->eqdt_count, &error_fatal);
        }
        break;

    case VST_TSEL_VPDT:

        /* There is a hack in skiboot to workaround DD1 issue with the
         * VPT setting in the VC engine in DD1. Skip it, we will get
         * it from the PC engine anyhow */
        if (!xive->vpdt && pc_engine) {

            /* VPDT is indirect */
            assert(indirect);

            /* FIXME: skiboot set the VPDT as indirect with 64K
             * subpages.
             */
            val &= ~VSD_TSIZE;
            val |= SETFIELD(VSD_TSIZE, 0ull, 0);
            size = 0x1000;

            xive->vpdt_count = size * VP_PER_PAGE / 8;

            xive->vpdt = g_malloc0(size);

            /* should be all NULL pointers */
            cpu_physical_memory_read(addr, xive->vpdt, size);

            xive->vsds[xive->vst_tsel] = val;
        }
        break;
    case VST_TSEL_IRQ:
        /* TODO */
        xive->vsds[xive->vst_tsel] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid table type %d\n",
                      xive->vst_tsel);
        return;
    }
}

/*
 * Some accesses to the TIMA are sometime done from some other thread
 * context. For resets.
 */
static void pnv_xive_thread_indirect_set(PnvXive *xive, uint64_t val)
{
    int pir = GETFIELD(PC_TCTXT_INDIR_THRDID, xive->regs[PC_TCTXT_INDIR0 >> 3]);

    if (val & PC_TCTXT_INDIR_VALID) {
        if (xive->cpu_ind) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: indirect access already set "
                          " for invalid PIR %d", pir);
        }

        pir = GETFIELD(PC_TCTXT_INDIR_THRDID, val) & 0xff;
        xive->cpu_ind = ppc_get_vcpu_by_pir(pir);
        if (!xive->cpu_ind) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid PIR %d for"
                          " indirect access\n", pir);
        }
    } else {
        xive->cpu_ind = NULL;
    }
}

/*
 * Interrupt Controller MMIO
 */
static void pnv_xive_ic_reg_write(PnvXive *xive, uint32_t offset, uint64_t val,
                               bool mmio)
{
    uint32_t reg = offset >> 3;

    switch (offset) {
    case CQ_CFG_PB_GEN:
    case CQ_MSGSND:
    case CQ_PBI_CTL:
    case CQ_FIRMASK_OR:

    case PC_TCTXT_CFG:
    case PC_TCTXT_TRACK:
    case PC_TCTXT_INDIR1:
    case PC_TCTXT_INDIR2:
    case PC_TCTXT_INDIR3:
    case PC_GLOBAL_CONFIG:
        /* set indirect mode for VSDs */

    case PC_VPC_SCRUB_MASK:
    case PC_VPC_CWATCH_SPEC:
    case VC_GLOBAL_CONFIG:
        /* set indirect mode for VSDs */

    case VC_AIB_TX_ORDER_TAG2:

    case VC_IRQ_CONFIG_IPI:
    case VC_IRQ_CONFIG_HW:
    case VC_IRQ_CONFIG_CASCADE1:
    case VC_IRQ_CONFIG_CASCADE2:
    case VC_IRQ_CONFIG_REDIST:
    case VC_IRQ_CONFIG_IPI_CASC:

    case VC_EQC_SCRUB_MASK:
    case VC_EQC_CWATCH_SPEC:
    case VC_EQC_CONFIG:
    case VC_IVC_SCRUB_MASK:
    case PC_AT_KILL_MASK:
    case VC_AT_MACRO_KILL_MASK:
        xive->regs[reg] = val;
        break;

    /* TODO: we could set the memory region when the BAR are
     * configured by firmware instead of hardcoding the adddr/size
     * values when the object is realized.
     */
    case CQ_IC_BAR: /* IC BAR and page size. 8 * 64k */
        xive->regs[reg] = val;
        break;

    case CQ_TM1_BAR: /* TM BAR and page size. 4 * 64k */
    case CQ_TM2_BAR: /* second TM BAR and page size. For hotplug use */
        xive->regs[reg] = val;
        break;

    case CQ_PC_BAR:
        xive->regs[reg] = val;
        break;

    case CQ_PC_BARM: /* PC BAR size */
        xive->regs[reg] = val;
        break;

    case CQ_VC_BAR:
        xive->regs[reg] = val;
        break;

    case CQ_VC_BARM: /* VC BAR size */
        xive->regs[reg] = val;
        break;

    case PC_AT_KILL:
        /* TODO: reload vpdt because pages were cleared */
        xive->regs[reg] |= val;
        break;

    case VC_AT_MACRO_KILL:
        /* TODO: reload eddt because pages were cleared */
        xive->regs[reg] |= val;
        break;

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

    case PC_TCTXT_INDIR0: /* set up CPU for indirect TIMA access */
        pnv_xive_thread_indirect_set(xive, val);
        xive->regs[reg] = val;
        break;

    case CQ_TAR: /* Set Translation Table Address */
        pnv_xive_set_xlate_select(xive, val);
        break;

    case CQ_TDR: /* Set Translation Table Data */
        pnv_xive_set_xlate_update(xive, val);
        break;

    case VC_IVC_SCRUB_TRIG:
        pnv_xive_ive_update(xive, GETFIELD(VC_SCRUB_OFFSET, val));
        break;

    case PC_VPC_CWATCH_DAT0:
    case PC_VPC_CWATCH_DAT1:
    case PC_VPC_CWATCH_DAT2:
    case PC_VPC_CWATCH_DAT3:
    case PC_VPC_CWATCH_DAT4:
    case PC_VPC_CWATCH_DAT5:
    case PC_VPC_CWATCH_DAT6:
    case PC_VPC_CWATCH_DAT7: /* XiveVP data for update */
        xive->vpc_watch[(offset - PC_VPC_CWATCH_DAT0) / 8] = cpu_to_be64(val);
        break;

    case PC_VPC_SCRUB_TRIG:
        pnv_xive_vp_update(xive, GETFIELD(PC_SCRUB_OFFSET, val));
        break;

    case VC_EQC_CWATCH_DAT0:
    case VC_EQC_CWATCH_DAT1:
    case VC_EQC_CWATCH_DAT2:
    case VC_EQC_CWATCH_DAT3: /* XiveEQ data for update */
        xive->eqc_watch[(offset - VC_EQC_CWATCH_DAT0) / 8] = cpu_to_be64(val);
        break;

    case VC_EQC_SCRUB_TRIG:
        pnv_xive_eq_update(xive, GETFIELD(VC_SCRUB_OFFSET, val));
        break;

    case VC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_ADDR:
        xive->vst_tsel = GETFIELD(VST_TABLE_SELECT, val);
        xive->vst_tidx = GETFIELD(VST_TABLE_OFFSET, val);
        break;

    case VC_VSD_TABLE_DATA:
        pnv_xive_table_set_data(xive, val, false);
        break;

    case PC_VSD_TABLE_DATA:
        pnv_xive_table_set_data(xive, val, true);
        break;

    case VC_SBC_CONFIG:
        xive->regs[reg] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE/IC: invalid writing to reg=0x%08x"
                      " mmio=%d\n", offset, mmio);
    }
}

static uint64_t pnv_xive_ic_reg_read(PnvXive *xive, uint32_t offset, bool mmio)
{
    uint64_t val = 0;
    uint32_t reg = offset >> 3;

    switch (offset) {
    case CQ_CFG_PB_GEN:
    case CQ_MSGSND: /* activated cores */
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
    case PC_VPC_SCRUB_TRIG:
    case VC_IVC_SCRUB_TRIG:
    case VC_EQC_SCRUB_TRIG:
        xive->regs[reg] &= ~VC_SCRUB_VALID;
        val = xive->regs[reg];
        break;
    case VC_EQC_CONFIG:
        val = SYNC_MASK;
        break;
    case PC_AT_KILL:
        xive->regs[reg] &= ~PC_AT_KILL_VALID;
        val = xive->regs[reg];
        break;
    case VC_AT_MACRO_KILL:
        xive->regs[reg] &= ~VC_KILL_VALID;
        val = xive->regs[reg];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE/IC: invalid read reg=0x%08x"
                      " mmio=%d\n", offset, mmio);
    }

    return val;
}

/*
 * Interrupt Controller MMIO: Notify ports
 */
static void pnv_xive_ic_notify_write(PnvXive *xive, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xive);

    xfc->notify(XIVE_FABRIC(xive), val);
}

/*
 * Interrupt Controller MMIO: Synchronisation registers
 */
#define PNV_XIVE_SYNC_IPI       0x400 /* Sync IPI */
#define PNV_XIVE_SYNC_HW        0x480 /* Sync HW */
#define PNV_XIVE_SYNC_OS_ESC    0x500 /* Sync OS escalations */
#define PNV_XIVE_SYNC_HW_ESC    0x580 /* Sync Hyp escalations */
#define PNV_XIVE_SYNC_REDIS     0x600 /* Sync Redistribution */

static void pnv_xive_ic_sync_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{

    switch (addr) {
    case PNV_XIVE_SYNC_IPI:
    case PNV_XIVE_SYNC_HW:
    case PNV_XIVE_SYNC_OS_ESC:
    case PNV_XIVE_SYNC_HW_ESC:
    case PNV_XIVE_SYNC_REDIS:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE/IC: invalid sync @%"
                      HWADDR_PRIx"\n", addr);
    }
}

/*
 * Interrupt controller MMIO regions
 *
 * 0x00000 - 0x0FFFF : BARs
 * 0x10000 - 0x107FF : Notify ports
 * 0x10800 - 0x10FFF : Synchronisation registers
 * 0x40000 - 0x7FFFF : indirect TIMA
 */
static void pnv_xive_ic_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    switch (addr) {
    case 0x00000 ... 0x0FFFF:
        pnv_xive_ic_reg_write(opaque, addr, val, true);
        break;
    case 0x10000 ... 0x107FF:
        pnv_xive_ic_notify_write(opaque, addr - 0x10000, val, size);
        break;
    case 0x10800 ... 0x10FFF:
        pnv_xive_ic_sync_write(opaque, addr - 0x10800, val, size);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE/IC: invalid write @%"
                      HWADDR_PRIx"\n", addr);
        break;
    }
}

static uint64_t pnv_xive_ic_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t ret = 0;

    switch (addr) {
    case 0x00000 ... 0x0FFFF:
        ret = pnv_xive_ic_reg_read(opaque, addr, true);
        break;
    case 0x10800 ... 0x10FFF:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: read IC notify port addr @%"
                      HWADDR_PRIx"\n", addr);
        break;
    case 0x10000 ... 0x107FF:
        /* no writes on synchronisation registers */
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE/IC: invalid read @%"
                      HWADDR_PRIx"\n", addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps pnv_xive_ic_ops = {
    .read = pnv_xive_ic_read,
    .write = pnv_xive_ic_write,
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

/*
 * Interrupt controller XSCOM regions. Accesses can nearly all be
 * redirected to the MMIO region.
 */
static uint64_t pnv_xive_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (addr >> 3) {
    case X_VC_EQC_CONFIG:
        /* This is the only XSCOM load done in skiboot. To be checked. */
        return SYNC_MASK;
    default:
        return pnv_xive_ic_reg_read(opaque, addr, false);
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

/* TODO: finish reconciliating with XIVE generic routing routine */
static void pnv_xive_notify(XiveFabric *xf, uint32_t lisn)
{
    PnvXive *xive = PNV_XIVE(xf);
    XiveIVE *ive;
    XiveEQ eq;
    uint32_t eq_idx;
    uint8_t priority;
    uint32_t nvt_idx;
    XiveNVT *nvt;

    ive = xive_fabric_get_ive(xf, lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", lisn);
        return;
    }

    if (ive->w & IVE_MASKED) {
        return;
    }

    /* Find our XiveEQ */
    eq_idx = GETFIELD(IVE_EQ_INDEX, ive->w);
    if (pnv_xive_eq_get(xive, eq_idx, &eq)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No EQ %d\n", eq_idx);
        return;
    }

    if (!(eq.w0 & EQ_W0_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No valid EQ for LISN %d\n", lisn);
        return;
    }

    if (eq.w0 & EQ_W0_ENQUEUE) {
        xive_eq_push(&eq, GETFIELD(IVE_EQ_DATA, ive->w));
        pnv_xive_eq_set(xive, eq_idx, &eq);
    }
    if (!(eq.w0 & EQ_W0_UCOND_NOTIFY)) {
        qemu_log_mask(LOG_UNIMP, "XIVE: !UCOND_NOTIFY not implemented\n");
    }

    nvt_idx = GETFIELD(EQ_W6_NVT_INDEX, eq.w6);
    nvt = xive_fabric_get_nvt(xf, nvt_idx);
    if (!nvt) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVT for idx %d\n", nvt_idx);
        return;
    }

    if (GETFIELD(EQ_W6_FORMAT_BIT, eq.w6) == 0) {
        priority = GETFIELD(EQ_W7_F0_PRIORITY, eq.w7);

        /* The EQ is masked. Can this happen ?  */
        if (priority == 0xff) {
            return;
        }

        /* Update the IPB (Interrupt Pending Buffer) with the priority
         * of the new notification. HW uses MMIOs to update the VP
         * structures. Something to address later.
         */
        xive_nvt_hv_ipb_update(nvt, priority);
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: w7 format1 not implemented\n");
    }

    xive_nvt_hv_notify(nvt);
}

/*
 * Virtualization Controller MMIO region. It contain the ESB pages for
 * the IPIs interrupts and ESB pages for the EQs. The split is done
 * with the set translation tables.
 */
static uint64_t pnv_xive_vc_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE/VC: invalid read @%"
                  HWADDR_PRIx"\n", offset);

    /* if out of scope, specs says to return all ones */
    return -1;
}

static void pnv_xive_vc_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE/VC: invalid write @%"
                  HWADDR_PRIx" val=0x%"PRIx64"\n", offset, value);
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
 * Presenter Controller MMIO region. This is used by the
 * Virtualization Controller to update the IPB and the NVT (XiveVP)
 * table when required. Not implemented yet.
 */
static uint64_t pnv_xive_pc_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE/PC: invalid read @%"HWADDR_PRIx"\n",
                  addr);
    return -1;
}

static void pnv_xive_pc_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE/PC: invalid write @%"HWADDR_PRIx
                  " val=0x%"PRIx64"\n", offset, value);
}

static const MemoryRegionOps pnv_xive_pc_ops = {
    .read = pnv_xive_pc_read,
    .write = pnv_xive_pc_write,
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

void pnv_xive_pic_print_info(PnvXive *xive, Monitor *mon)
{
    int i;

    monitor_printf(mon, "IVE Table\n");
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];
        uint32_t eq_idx;

        if (!(ive->w & IVE_VALID)) {
            continue;
        }

        eq_idx = GETFIELD(IVE_EQ_INDEX, ive->w);

        monitor_printf(mon, " %6x %s eqidx:%d ", i,
                       ive->w & IVE_MASKED ? "M" : " ",
                       eq_idx);


        if (!(ive->w & IVE_MASKED)) {
            XiveEQ eq;

            if (!pnv_xive_eq_get(xive, eq_idx, &eq)) {
                xive_eq_pic_print_info(&eq, mon);
                monitor_printf(mon, " data:%08x",
                               (int) GETFIELD(IVE_EQ_DATA, ive->w));
            } else {
                monitor_printf(mon, "no eq ?!");
            }
        }
        monitor_printf(mon, "\n");
    }

    xive_source_pic_print_info(&xive->source, mon);
}

static void pnv_xive_reset(DeviceState *dev)
{
    PnvXive *xive = PNV_XIVE(dev);
    int i;

    device_reset(DEVICE(&xive->source));
    device_reset(DEVICE(&xive->eq_source));

    /* Mask all valid IVEs in the IRQ number space. */
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];
        if (ive->w & IVE_VALID) {
            ive->w |= IVE_MASKED;
        }
    }
}

static void pnv_xive_init(Object *obj)
{
    PnvXive *xive = PNV_XIVE(obj);

    object_initialize(&xive->source, sizeof(xive->source), TYPE_XIVE_SOURCE);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);

    object_initialize(&xive->eq_source, sizeof(xive->eq_source),
                      TYPE_XIVE_SOURCE);
    object_property_add_child(obj, "eq_source", OBJECT(&xive->eq_source), NULL);
}

static void pnv_xive_realize(DeviceState *dev, Error **errp)
{
    PnvXive *xive = PNV_XIVE(dev);

    /* XSCOM region */
    memory_region_init_io(&xive->xscom_regs, OBJECT(dev), &pnv_xive_xscom_ops,
                          xive, "xscom-xive", PNV_XSCOM_XIVE_SIZE << 3);

    /* Interrupt controller MMIO region */
    memory_region_init_io(&xive->ic_mmio, OBJECT(dev), &pnv_xive_ic_ops, xive,
                          "xive.ic", PNV_XIVE_IC_SIZE);

    /* Overall Virtualization Controller MMIO region.  */
    memory_region_init_io(&xive->vc_mmio, OBJECT(xive), &pnv_xive_vc_ops, xive,
                          "xive.vc", PNV_XIVE_VC_SIZE);

    /* Virtualization Controller subregions for IPIs & EQs. Their
     * sizes and offsets will be configured later when the translation
     * sets are established
     */
    xive->esb_base = xive->vc_base;
    memory_region_init_io(&xive->esb_mmio, OBJECT(xive), NULL, xive,
                          "xive.vc.esb", 0);
    memory_region_add_subregion(&xive->vc_mmio, 0, &xive->esb_mmio);

    xive->eq_base = xive->vc_base;
    memory_region_init_io(&xive->eq_mmio, OBJECT(xive), NULL, xive,
                          "xive.vc.eq", 0);
    memory_region_add_subregion(&xive->vc_mmio, 0, &xive->eq_mmio);

    /* Thread Interrupt Management Area */
    memory_region_init_io(&xive->tm_mmio, OBJECT(xive), &xive_tm_hv_ops,
                          &xive->cpu_ind, "xive.tima", PNV_XIVE_TM_SIZE);
    memory_region_init_alias(&xive->tm_mmio_indirect, OBJECT(xive),
                             "xive.tima.indirect",
                             &xive->tm_mmio, 0, PNV_XIVE_TM_SIZE);

    /* Presenter Controller MMIO region */
    memory_region_init_io(&xive->pc_mmio, OBJECT(xive), &pnv_xive_pc_ops, xive,
                          "xive.pc", PNV_XIVE_PC_SIZE);

    /* Map all regions from the XIVE model realize routine. This is
     * simpler than from the machine
     */
    memory_region_add_subregion(get_system_memory(), xive->ic_base,
                                &xive->ic_mmio);
    memory_region_add_subregion(get_system_memory(), xive->ic_base + 0x40000,
                                &xive->tm_mmio_indirect);
    memory_region_add_subregion(get_system_memory(), xive->vc_base,
                                &xive->vc_mmio);
    memory_region_add_subregion(get_system_memory(), xive->pc_base,
                                &xive->pc_mmio);
    memory_region_add_subregion(get_system_memory(), xive->tm_base,
                                &xive->tm_mmio);
}

static int pnv_xive_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power9-xive-x";
    char *name;
    int offset;
    uint32_t lpc_pcba = PNV_XSCOM_XIVE_BASE;
    uint32_t reg[] = {
        cpu_to_be32(lpc_pcba),
        cpu_to_be32(PNV_XSCOM_XIVE_SIZE)
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

static XiveNVT *pnv_xive_get_nvt(XiveFabric *xf, uint32_t nvt_idx)
{
    PnvXive *xive = PNV_XIVE(xf);
    int server;
    PowerPCCPU *cpu;
    XiveVP vp;

    /* only use the VP to check the valid bit */
    if (pnv_xive_vp_get(xive, nvt_idx, &vp)) {
        return NULL;
    }

    if (!(vp.w0 & VP_W0_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: VP idx %x is invalid\n", nvt_idx);
        return NULL;
    }

    /* TODO: quick and dirty NVT-to-server decoding ... This needs
     * more care. */
    server = nvt_idx & 0x7f;
    cpu = ppc_get_vcpu_by_pir(server);

    return cpu ? XIVE_NVT(cpu->intc) : NULL;
}

static XiveIVE *pnv_xive_get_ive(XiveFabric *xf, uint32_t lisn)
{
    PnvXive *xive = PNV_XIVE(xf);

    return lisn < xive->nr_irqs ? &xive->ivt[lisn] : NULL;
}

static void pnv_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(klass);

    xdc->dt_xscom = pnv_xive_dt_xscom;

    dc->desc = "PowerNV XIVE Interrupt Controller";
    dc->realize = pnv_xive_realize;
    dc->props = pnv_xive_properties;
    dc->reset = pnv_xive_reset;

    xfc->get_ive = pnv_xive_get_ive;
    xfc->get_nvt = pnv_xive_get_nvt;
    /* TODO : xfc->get_eq */
    xfc->notify = pnv_xive_notify;
};

static const TypeInfo pnv_xive_info = {
    .name          = TYPE_PNV_XIVE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = pnv_xive_init,
    .instance_size = sizeof(PnvXive),
    .class_init    = pnv_xive_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { TYPE_XIVE_FABRIC },
        { }
    }
};

static void pnv_xive_register_types(void)
{
    type_register_static(&pnv_xive_info);
}

type_init(pnv_xive_register_types)

void pnv_chip_xive_realize(PnvChip *chip, Error **errp)
{
    Object *obj;
    Error *local_err = NULL;

    obj = object_new(TYPE_PNV_XIVE);
    qdev_set_parent_bus(DEVICE(obj), sysbus_get_default());

    object_property_add_child(OBJECT(chip), "xive", obj, &error_abort);
    object_property_set_int(obj, PNV_XIVE_IC_BASE(chip), "ic-bar",
                            &error_fatal);
    object_property_set_int(obj, PNV_XIVE_VC_BASE(chip), "vc-bar",
                            &error_fatal);
    object_property_set_int(obj, PNV_XIVE_PC_BASE(chip), "pc-bar",
                            &error_fatal);
    object_property_set_int(obj, PNV_XIVE_TM_BASE(chip), "tm-bar",
                            &error_fatal);
    object_property_set_bool(obj, true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    chip->xive = PNV_XIVE(obj);

    pnv_xscom_add_subregion(chip, PNV_XSCOM_XIVE_BASE,
                            &chip->xive->xscom_regs);
}
