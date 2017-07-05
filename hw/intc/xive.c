/*
 * QEMU PowerPC XIVE model
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/dma.h"
#include "monitor/monitor.h"
#include "hw/ppc/xive.h"

#include "xive-internal.h"

static uint8_t priority_to_ipb(uint8_t priority)
{
    return priority < XIVE_EQ_PRIORITY_COUNT ? 1 << (7 - priority) : 0;
}

static uint64_t xive_icp_accept(XiveICPState *xicp)
{
    ICPState *icp = ICP(xicp);
    uint8_t nsr = xicp->tima_os[TM_NSR];

    qemu_irq_lower(icp->output);

    if (xicp->tima_os[TM_NSR] & TM_QW1_NSR_EO) {
        uint8_t cppr = xicp->tima_os[TM_PIPR];

        xicp->tima_os[TM_CPPR] = cppr;

        /* Reset the pending buffer bit */
        xicp->tima_os[TM_IPB] &= ~priority_to_ipb(cppr);

        /* Drop Exception bit for OS */
        xicp->tima_os[TM_NSR] &= ~TM_QW1_NSR_EO;
    }

    return (nsr << 8) | xicp->tima_os[TM_CPPR];
}

static uint8_t ipb_to_pipr(uint8_t ibp)
{
    return ibp ? clz32((uint32_t)ibp << 24) : 0xff;
}

static void xive_icp_notify(XiveICPState *xicp)
{
    xicp->tima_os[TM_PIPR] = ipb_to_pipr(xicp->tima_os[TM_IPB]);

    if (xicp->tima_os[TM_PIPR] < xicp->tima_os[TM_CPPR]) {
        xicp->tima_os[TM_NSR] |= TM_QW1_NSR_EO;
        qemu_irq_raise(ICP(xicp)->output);
    }
}

static void xive_icp_set_cppr(XiveICPState *xicp, uint8_t cppr)
{
    if (cppr > XIVE_PRIORITY_MAX) {
        cppr = 0xff;
    }

    xicp->tima_os[TM_CPPR] = cppr;

    /* CPPR has changed, inform the ICP which might raise an
     * exception */
    xive_icp_notify(xicp);
}

/*
 * Thread Interrupt Management Area MMIO
 */
static uint64_t xive_tm_read_special(XiveICPState *icp, hwaddr offset,
                                     unsigned size)
{
    uint64_t ret = -1;

    if (offset == TM_SPC_ACK_OS_REG && size == 2) {
        ret = xive_icp_accept(icp);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA read @%"
                      HWADDR_PRIx" size %d\n", offset, size);
    }

    return ret;
}

static uint64_t xive_tm_read(void *opaque, hwaddr offset, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    XiveICPState *icp = XIVE_ICP(cpu->intc);
    uint64_t ret = -1;
    int i;

    if (offset >= TM_SPC_ACK_EBB) {
        return xive_tm_read_special(icp, offset, size);
    }

    if (offset & TM_QW1_OS) {
        switch (size) {
        case 1:
        case 2:
        case 4:
        case 8:
            if (QEMU_IS_ALIGNED(offset, size)) {
                ret = 0;
                for (i = 0; i < size; i++) {
                    ret |= icp->tima[offset + i] << (8 * i);
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "XIVE: invalid TIMA read alignment @%"
                              HWADDR_PRIx" size %d\n", offset, size);
            }
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: does handle non-OS TIMA ring @%"
                      HWADDR_PRIx"\n", offset);
    }

    return ret;
}

static bool xive_tm_is_readonly(uint8_t index)
{
    /* Let's be optimistic and prepare ground for HV mode support */
    switch (index) {
    case TM_QW1_OS + TM_CPPR:
        return false;
    default:
        return true;
    }
}

static void xive_tm_write_special(XiveICPState *xicp, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    if (offset == TM_SPC_SET_OS_PENDING && size == 1) {
        xicp->tima_os[TM_IPB] |= priority_to_ipb(value & 0xff);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA write @%"
                      HWADDR_PRIx" size %d\n", offset, size);
    }

    /* TODO: support TM_SPC_ACK_OS_EL */
}

static void xive_tm_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    XiveICPState *icp = XIVE_ICP(cpu->intc);
    int i;

    if (offset >= TM_SPC_ACK_EBB) {
        xive_tm_write_special(icp, offset, value, size);
        return;
    }

    if (offset & TM_QW1_OS) {
        switch (size) {
        case 1:
            if (offset == TM_QW1_OS + TM_CPPR) {
                xive_icp_set_cppr(icp, value & 0xff);
            }
            break;
        case 4:
        case 8:
            if (QEMU_IS_ALIGNED(offset, size)) {
                for (i = 0; i < size; i++) {
                    if (!xive_tm_is_readonly(offset + i)) {
                        icp->tima[offset + i] = (value >> (8 * i)) & 0xff;
                    }
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA write @%"
                              HWADDR_PRIx" size %d\n", offset, size);
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA write @%"
                          HWADDR_PRIx" size %d\n", offset, size);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: does handle non-OS TIMA ring @%"
                      HWADDR_PRIx"\n", offset);
    }
}


static const MemoryRegionOps xive_tm_ops = {
    .read = xive_tm_read,
    .write = xive_tm_write,
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

static void xive_icp_reset(ICPState *icp)
{
    XiveICPState *xicp = XIVE_ICP(icp);

    memset(xicp->tima, 0, sizeof(xicp->tima));
}

static void xive_icp_print_info(ICPState *icp, Monitor *mon)
{
    XiveICPState *xicp = XIVE_ICP(icp);

    monitor_printf(mon, " CPPR=%02x IPB=%02x PIPR=%02x NSR=%02x\n",
                   xicp->tima_os[TM_CPPR], xicp->tima_os[TM_IPB],
                   xicp->tima_os[TM_PIPR], xicp->tima_os[TM_NSR]);
}

static void xive_icp_init(Object *obj)
{
    XiveICPState *xicp = XIVE_ICP(obj);

    xicp->tima_os = &xicp->tima[TM_QW1_OS];
}

static void xive_icp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ICPStateClass *icpc = ICP_CLASS(klass);

    dc->desc = "PowerNV Xive ICP";
    icpc->reset = xive_icp_reset;
    icpc->print_info = xive_icp_print_info;
}

static const TypeInfo xive_icp_info = {
    .name          = TYPE_XIVE_ICP,
    .parent        = TYPE_ICP,
    .instance_size = sizeof(XiveICPState),
    .instance_init = xive_icp_init,
    .class_init    = xive_icp_class_init,
    .class_size    = sizeof(ICPStateClass),
};

static XiveICPState *xive_icp_get(XICSFabric *xi, int server)
{
    XICSFabricClass *xic = XICS_FABRIC_GET_CLASS(xi);
    ICPState *icp = xic->icp_get(xi, server);

    return XIVE_ICP(icp);
}

static void xive_eq_push(XiveEQ *eq, uint32_t data)
{
    uint64_t qaddr_base = (((uint64_t)(eq->w2 & 0x0fffffff)) << 32) | eq->w3;
    uint32_t qsize = GETFIELD(EQ_W0_QSIZE, eq->w0);
    uint32_t qindex = GETFIELD(EQ_W1_PAGE_OFF, eq->w1);
    uint32_t qgen = GETFIELD(EQ_W1_GENERATION, eq->w1);

    uint64_t qaddr = qaddr_base + (qindex << 2);
    uint32_t qdata = cpu_to_be32((qgen << 31) | (data & 0x7fffffff));
    uint32_t qentries = 1 << (qsize + 10);

    if (dma_memory_write(&address_space_memory, qaddr, &qdata, sizeof(qdata))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed to write EQ data @0x%"
                      HWADDR_PRIx "\n", __func__, qaddr);
        return;
    }

    qindex = (qindex + 1) % qentries;
    if (qindex == 0) {
        qgen ^= 1;
        eq->w1 = SETFIELD(EQ_W1_GENERATION, eq->w1, qgen);
    }
    eq->w1 = SETFIELD(EQ_W1_PAGE_OFF, eq->w1, qindex);
}

static void xive_icp_irq(XiveICSState *xs, int lisn)
{
    XIVE *x = xs->xive;
    XiveICPState *xicp;
    XiveIVE *ive;
    XiveEQ *eq;
    uint32_t eq_idx;
    uint32_t priority;
    uint32_t target;

    ive = xive_get_ive(x, lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", lisn);
        return;
    }

    if (ive->w & IVE_MASKED) {
        return;
    }

    /* Find our XiveEQ */
    eq_idx = GETFIELD(IVE_EQ_INDEX, ive->w);
    eq = xive_get_eq(x, eq_idx);
    if (!eq) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No EQ for LISN %d\n", lisn);
        return;
    }

    if (eq->w0 & EQ_W0_ENQUEUE) {
        xive_eq_push(eq, GETFIELD(IVE_EQ_DATA, ive->w));
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: !ENQUEUE not implemented\n");
    }

    if (!(eq->w0 & EQ_W0_UCOND_NOTIFY)) {
        qemu_log_mask(LOG_UNIMP, "XIVE: !UCOND_NOTIFY not implemented\n");
    }

    target = GETFIELD(EQ_W6_NVT_INDEX, eq->w6);

    /* use the XICSFabric (machine) to get the ICP */
    xicp = xive_icp_get(ICS_BASE(xs)->xics, target);
    if (!xicp) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No ICP for target %d\n", target);
        return;
    }

    if (GETFIELD(EQ_W6_FORMAT_BIT, eq->w6) == 0) {
        priority = GETFIELD(EQ_W7_F0_PRIORITY, eq->w7);

        /* The EQ is masked. Can this happen ?  */
        if (priority == 0xff) {
            return;
        }

        /* Update the IPB (Interrupt Pending Buffer) with the priority
         * of the new notification and inform the ICP, which will
         * decide to raise the exception, or not, depending on its
         * current CPPR value.
         */
        xicp->tima_os[TM_IPB] |= priority_to_ipb(priority);
    } else {
        qemu_log_mask(LOG_UNIMP, "XIVE: w7 format1 not implemented\n");
    }

    xive_icp_notify(xicp);
}

/*
 * "magic" Event State Buffer (ESB) MMIO offsets.
 *
 * Each interrupt source has a 2-bit state machine called ESB
 * which can be controlled by MMIO. It's made of 2 bits, P and
 * Q. P indicates that an interrupt is pending (has been sent
 * to a queue and is waiting for an EOI). Q indicates that the
 * interrupt has been triggered while pending.
 *
 * This acts as a coalescing mechanism in order to guarantee
 * that a given interrupt only occurs at most once in a queue.
 *
 * When doing an EOI, the Q bit will indicate if the interrupt
 * needs to be re-triggered.
 *
 * The following offsets into the ESB MMIO allow to read or
 * manipulate the PQ bits. They must be used with an 8-bytes
 * load instruction. They all return the previous state of the
 * interrupt (atomically).
 *
 * Additionally, some ESB pages support doing an EOI via a
 * store at 0 and some ESBs support doing a trigger via a
 * separate trigger page.
 */
#define XIVE_ESB_GET            0x800
#define XIVE_ESB_SET_PQ_00      0xc00
#define XIVE_ESB_SET_PQ_01      0xd00
#define XIVE_ESB_SET_PQ_10      0xe00
#define XIVE_ESB_SET_PQ_11      0xf00

#define XIVE_ESB_VAL_P          0x2
#define XIVE_ESB_VAL_Q          0x1

#define XIVE_ESB_RESET          0x0
#define XIVE_ESB_PENDING        0x2
#define XIVE_ESB_QUEUED         0x3
#define XIVE_ESB_OFF            0x1

static uint8_t xive_pq_get(XIVE *x, uint32_t lisn)
{
    uint32_t idx = lisn;
    uint32_t byte = idx / 4;
    uint32_t bit  = (idx % 4) * 2;
    uint8_t* pqs = (uint8_t *) x->sbe;

    return (pqs[byte] >> bit) & 0x3;
}

static void xive_pq_set(XIVE *x, uint32_t lisn, uint8_t pq)
{
    uint32_t idx = lisn;
    uint32_t byte = idx / 4;
    uint32_t bit  = (idx % 4) * 2;
    uint8_t* pqs = (uint8_t *) x->sbe;

    pqs[byte] &= ~(0x3 << bit);
    pqs[byte] |= (pq & 0x3) << bit;
}

static bool xive_pq_eoi(XIVE *x, uint32_t lisn)
{
    uint8_t old_pq = xive_pq_get(x, lisn);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        xive_pq_set(x, lisn, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_PENDING:
        xive_pq_set(x, lisn, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_QUEUED:
        xive_pq_set(x, lisn, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_OFF:
        xive_pq_set(x, lisn, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

static bool xive_pq_trigger(XIVE *x, uint32_t lisn)
{
    uint8_t old_pq = xive_pq_get(x, lisn);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        xive_pq_set(x, lisn, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_PENDING:
        xive_pq_set(x, lisn, XIVE_ESB_QUEUED);
        return true;
    case XIVE_ESB_QUEUED:
        xive_pq_set(x, lisn, XIVE_ESB_QUEUED);
        return true;
    case XIVE_ESB_OFF:
        xive_pq_set(x, lisn, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

/*
 * XIVE Interrupt Source MMIOs
 */
static void xive_ics_eoi(XiveICSState *xs, uint32_t srcno)
{
    ICSIRQState *irq = &ICS_BASE(xs)->irqs[srcno];

    if (irq->flags & XICS_FLAGS_IRQ_LSI) {
        irq->status &= ~XICS_STATUS_SENT;
    }
}

/* TODO: handle second page */
static uint64_t xive_esb_read(void *opaque, hwaddr addr, unsigned size)
{
    XiveICSState *xs = ICS_XIVE(opaque);
    XIVE *x = xs->xive;
    uint32_t offset = addr & 0xF00;
    uint32_t srcno = addr >> xs->esb_shift;
    uint32_t lisn = srcno + ICS_BASE(xs)->offset;
    XiveIVE *ive;
    uint64_t ret = -1;

    ive = xive_get_ive(x, lisn);
    if (!ive || !(ive->w & IVE_VALID))  {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", lisn);
        goto out;
    }

    if (srcno >= ICS_BASE(xs)->nr_irqs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: invalid IRQ number: %d/%d lisn: %d\n",
                      srcno, ICS_BASE(xs)->nr_irqs, lisn);
        goto out;
    }

    switch (offset) {
    case 0:
        xive_ics_eoi(xs, srcno);

        /* return TRUE or FALSE depending on PQ value */
        ret = xive_pq_eoi(x, lisn);
        break;

    case XIVE_ESB_GET:
        ret = xive_pq_get(x, lisn);
        break;

    case XIVE_ESB_SET_PQ_00:
    case XIVE_ESB_SET_PQ_01:
    case XIVE_ESB_SET_PQ_10:
    case XIVE_ESB_SET_PQ_11:
        ret = xive_pq_get(x, lisn);
        xive_pq_set(x, lisn, (offset >> 8) & 0x3);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB addr %d\n", offset);
    }

out:
    return ret;
}

static void xive_esb_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    XiveICSState *xs = ICS_XIVE(opaque);
    XIVE *x = xs->xive;
    uint32_t offset = addr & 0xF00;
    uint32_t srcno = addr >> xs->esb_shift;
    uint32_t lisn = srcno + ICS_BASE(xs)->offset;
    XiveIVE *ive;
    bool notify = false;

    ive = xive_get_ive(x, lisn);
    if (!ive || !(ive->w & IVE_VALID))  {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", lisn);
        return;
    }

    if (srcno >= ICS_BASE(xs)->nr_irqs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: invalid IRQ number: %d/%d lisn: %d\n",
                      srcno, ICS_BASE(xs)->nr_irqs, lisn);
        return;
    }

    switch (offset) {
    case 0:
        /* TODO: should we trigger even if the IVE is masked ? */
        notify = xive_pq_trigger(x, lisn);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB write addr %d\n",
                      offset);
        return;
    }

    if (notify && !(ive->w & IVE_MASKED)) {
        qemu_irq_pulse(ICS_BASE(xs)->qirqs[srcno]);
    }
}

static const MemoryRegionOps xive_esb_ops = {
    .read = xive_esb_read,
    .write = xive_esb_write,
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
 * XIVE Interrupt Source
 */
static void xive_ics_set_irq_msi(XiveICSState *xs, int srcno, int val)
{
    if (val) {
        xive_icp_irq(xs, srcno + ICS_BASE(xs)->offset);
    }
}

static void xive_ics_set_irq_lsi(XiveICSState *xs, int srcno, int val)
{
    ICSIRQState *irq = &ICS_BASE(xs)->irqs[srcno];

    if (val) {
        irq->status |= XICS_STATUS_ASSERTED;
    } else {
        irq->status &= ~XICS_STATUS_ASSERTED;
    }

    if (irq->status & XICS_STATUS_ASSERTED
        && !(irq->status & XICS_STATUS_SENT)) {
        irq->status |= XICS_STATUS_SENT;
        xive_icp_irq(xs, srcno + ICS_BASE(xs)->offset);
    }
}

static void xive_ics_set_irq(void *opaque, int srcno, int val)
{
    XiveICSState *xs = ICS_XIVE(opaque);
    ICSIRQState *irq = &ICS_BASE(xs)->irqs[srcno];

    if (irq->flags & XICS_FLAGS_IRQ_LSI) {
        xive_ics_set_irq_lsi(xs, srcno, val);
    } else {
        xive_ics_set_irq_msi(xs, srcno, val);
    }
}

static void xive_ics_print_info(ICSState *ics, Monitor *mon)
{
    XiveICSState *xs = ICS_XIVE(ics);
    int i;

    for (i = 0; i < ics->nr_irqs; i++) {
        ICSIRQState *irq = ics->irqs + i;

        if (!(irq->flags & XICS_FLAGS_IRQ_MASK)) {
            continue;
        }
        monitor_printf(mon, "  %4x %s pq=%02x status=%02x\n",
                       ics->offset + i,
                       (irq->flags & XICS_FLAGS_IRQ_LSI) ? "LSI" : "MSI",
                       xive_pq_get(xs->xive, ics->offset + i),
                       irq->status);
    }
}

static void xive_ics_reset(void *dev)
{
    ICSState *ics = ICS_BASE(dev);
    int i;
    uint8_t flags[ics->nr_irqs];

    for (i = 0; i < ics->nr_irqs; i++) {
        flags[i] = ics->irqs[i].flags;
    }

    memset(ics->irqs, 0, sizeof(ICSIRQState) * ics->nr_irqs);

    for (i = 0; i < ics->nr_irqs; i++) {
        ics->irqs[i].flags = flags[i];
    }
}

static void xive_ics_realize(ICSState *ics, Error **errp)
{
    XiveICSState *xs = ICS_XIVE(ics);
    Object *obj;
    Error *err = NULL;
    XIVE *x;

    obj = object_property_get_link(OBJECT(xs), "xive", &err);
    if (!obj) {
        error_setg(errp, "%s: required link 'xive' not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }
    x = xs->xive = XIVE(obj);

    if (!ics->nr_irqs) {
        error_setg(errp, "Number of interrupts needs to be greater 0");
        return;
    }

    if (!xs->esb_shift) {
        error_setg(errp, "ESB page size needs to be greater 0");
        return;
    }

    ics->irqs = g_malloc0(ics->nr_irqs * sizeof(ICSIRQState));
    ics->qirqs = qemu_allocate_irqs(xive_ics_set_irq, xs, ics->nr_irqs);

    memory_region_init_io(&xs->esb_iomem, OBJECT(xs), &xive_esb_ops, xs,
                          "xive.esb",
                          (1ull << xs->esb_shift) * ICS_BASE(xs)->nr_irqs);

    /* Install the ESB memory region in the overall one */
    memory_region_add_subregion(&x->esb_iomem,
                                ICS_BASE(xs)->offset * (1 << xs->esb_shift),
                                &xs->esb_iomem);

    /* Record base address which is needed by the hcalls */
    xs->esb_base = x->vc_base + ICS_BASE(xs)->offset * (1 << xs->esb_shift);

    qemu_register_reset(xive_ics_reset, xs);
}

static Property xive_ics_properties[] = {
    DEFINE_PROP_UINT32("nr-irqs", ICSState, nr_irqs, 0),
    DEFINE_PROP_UINT32("irq-base", ICSState, offset, 0),
    DEFINE_PROP_UINT32("shift", XiveICSState, esb_shift, 0),
    DEFINE_PROP_UINT64("flags", XiveICSState, flags, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_ics_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ICSStateClass *isc = ICS_BASE_CLASS(klass);

    isc->realize = xive_ics_realize;
    isc->print_info = xive_ics_print_info;

    dc->props = xive_ics_properties;
}

static const TypeInfo xive_ics_info = {
    .name = TYPE_ICS_XIVE,
    .parent = TYPE_ICS_BASE,
    .instance_size = sizeof(XiveICSState),
    .class_init = xive_ics_class_init,
};

/*
 * Main XIVE object
 */

/* Let's provision some HW IRQ numbers. We could use a XIVE property
 * also but it does not seem necessary for the moment.
 */
#define MAX_HW_IRQS_ENTRIES (8 * 1024)

/* VC BAR contains set translations for the ESBs and the EQs. */
#define VC_BAR_DEFAULT   0x10000000000ull
#define VC_BAR_SIZE      0x08000000000ull

#define P9_MMIO_BASE     0x006000000000000ull
#define P9_CHIP_BASE(id) (P9_MMIO_BASE | (0x40000000000ull * (uint64_t) (id)))

/* Thread Interrupt Management Area MMIO */
#define TM_BAR_DEFAULT   0x30203180000ull
#define TM_SHIFT         16
#define TM_BAR_SIZE      (XIVE_TM_RING_COUNT * (1 << TM_SHIFT))

static uint64_t xive_esb_default_read(void *p, hwaddr offset, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " [%u]\n",
                  __func__, offset, size);
    return 0;
}

static void xive_esb_default_write(void *opaque, hwaddr offset, uint64_t value,
                unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " <- 0x%" PRIx64 " [%u]\n",
                  __func__, offset, value, size);
}

static const MemoryRegionOps xive_esb_default_ops = {
    .read = xive_esb_default_read,
    .write = xive_esb_default_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

void xive_reset(void *dev)
{
    XIVE *x = XIVE(dev);
    int i;

    /* SBEs are initialized to 0b01 which corresponds to "ints off" */
    memset(x->sbe, 0x55, x->int_count / 4);

    /* Clear and mask all valid IVEs */
    for (i = x->int_base; i < x->int_max; i++) {
        XiveIVE *ive = &x->ivt[i];
        if (ive->w & IVE_VALID) {
            ive->w = IVE_VALID | IVE_MASKED;
        }
    }

    /* clear all EQs */
    memset(x->eqdt, 0, x->nr_targets * XIVE_EQ_PRIORITY_COUNT * sizeof(XiveEQ));
}

static void xive_init(Object *obj)
{
    ;
}

static void xive_realize(DeviceState *dev, Error **errp)
{
    XIVE *x = XIVE(dev);

    if (!x->nr_targets) {
        error_setg(errp, "Number of interrupt targets needs to be greater 0");
        return;
    }

    /* Initialize IRQ number allocator. Let's use a base number if we
     * need to introduce a notion of blocks one day.
     */
    x->int_base = 0;
    x->int_count = x->nr_targets + MAX_HW_IRQS_ENTRIES;
    x->int_max = x->int_base + x->int_count;
    x->int_hw_bot = x->int_max;
    x->int_ipi_top = x->int_base;

    /* Reserve some numbers as OPAL does ? */
    if (x->int_ipi_top < 0x10) {
        x->int_ipi_top = 0x10;
    }

    /* Allocate SBEs (State Bit Entry). 2 bits, so 4 entries per byte */
    x->sbe = g_malloc0(x->int_count / 4);

    /* Allocate the IVT (Interrupt Virtualization Table) */
    x->ivt = g_malloc0(x->int_count * sizeof(XiveIVE));

    /* Allocate the EQDT (Event Queue Descriptor Table), 8 priorities
     * for each thread in the system */
    x->eqdt = g_malloc0(x->nr_targets * XIVE_EQ_PRIORITY_COUNT *
                        sizeof(XiveEQ));

    /* VC BAR. That's the full window but we will only map the
     * subregions in use. */
    x->vc_base = (hwaddr)(P9_CHIP_BASE(x->chip_id) | VC_BAR_DEFAULT);

    /* install default memory region handlers to log bogus access */
    memory_region_init_io(&x->esb_iomem, NULL, &xive_esb_default_ops,
                          NULL, "xive.esb", VC_BAR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &x->esb_iomem);

    /* TM BAR. Same address for each chip */
    x->tm_base = (P9_MMIO_BASE | TM_BAR_DEFAULT);
    x->tm_shift = TM_SHIFT;

    memory_region_init_io(&x->tm_iomem, OBJECT(x), &xive_tm_ops, x,
                          "xive.tm", TM_BAR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &x->tm_iomem);

    qemu_register_reset(xive_reset, dev);
}

static Property xive_properties[] = {
    DEFINE_PROP_UINT32("chip-id", XIVE, chip_id, 0),
    DEFINE_PROP_UINT32("nr-targets", XIVE, nr_targets, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xive_realize;
    dc->props = xive_properties;
    dc->desc = "XIVE";
}

static const TypeInfo xive_info = {
    .name = TYPE_XIVE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = xive_init,
    .instance_size = sizeof(XIVE),
    .class_init = xive_class_init,
};

static void xive_register_types(void)
{
    type_register_static(&xive_info);
    type_register_static(&xive_ics_info);
    type_register_static(&xive_icp_info);
}

type_init(xive_register_types)

XiveIVE *xive_get_ive(XIVE *x, uint32_t lisn)
{
    uint32_t idx = lisn;

    if (idx < x->int_base || idx >= x->int_max) {
        return NULL;
    }

    return &x->ivt[idx];
}

XiveEQ *xive_get_eq(XIVE *x, uint32_t idx)
{
    if (idx >= x->nr_targets * XIVE_EQ_PRIORITY_COUNT) {
        return NULL;
    }

    return &x->eqdt[idx];
}

/* TODO: improve EQ indexing. This is very simple and relies on the
 * fact that target (CPU) numbers start at 0 and are contiguous. It
 * should be OK for sPAPR.
 */
bool xive_eq_for_target(XIVE *x, uint32_t target, uint8_t priority,
                        uint32_t *out_eq_idx)
{
    if (priority > XIVE_PRIORITY_MAX || target >= x->nr_targets) {
        return false;
    }

    if (out_eq_idx) {
        *out_eq_idx = target + priority;
    }

    return true;
}
