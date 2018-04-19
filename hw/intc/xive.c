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
#include "hw/ppc/xics.h" /* for ICP_PROP_CPU */
#include "hw/ppc/xive.h"
#include "hw/ppc/xive_regs.h"

/*
 * XIVE Interrupt Presenter
 */

static uint64_t xive_nvt_accept(XiveNVT *nvt)
{
    return 0;
}

static void xive_nvt_set_cppr(XiveNVT *nvt, uint8_t cppr)
{
    if (cppr > XIVE_PRIORITY_MAX) {
        cppr = 0xff;
    }

    nvt->ring_os[TM_CPPR] = cppr;
}

/*
 * OS Thread Interrupt Management Area MMIO
 */
static uint64_t xive_tm_read_special(XiveNVT *nvt, hwaddr offset,
                                           unsigned size)
{
    uint64_t ret = -1;

    if (offset == TM_SPC_ACK_OS_REG && size == 2) {
        ret = xive_nvt_accept(nvt);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid TIMA read @%"
                      HWADDR_PRIx" size %d\n", offset, size);
    }

    return ret;
}

#define TM_RING(offset) ((offset) & 0xf0)

static uint64_t xive_tm_os_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    XiveNVT *nvt = XIVE_NVT(cpu->intc);
    uint64_t ret = -1;
    int i;

    if (offset >= TM_SPC_ACK_EBB) {
        return xive_tm_read_special(nvt, offset, size);
    }

    if (TM_RING(offset) != TM_QW1_OS) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid access to non-OS ring @%"
                      HWADDR_PRIx"\n", offset);
        return ret;
    }

    ret = 0;
    for (i = 0; i < size; i++) {
        ret |= (uint64_t) nvt->regs[offset + i] << (8 * (size - i - 1));
    }

    return ret;
}

static bool xive_tm_is_readonly(uint8_t offset)
{
    return offset != TM_QW1_OS + TM_CPPR;
}

static void xive_tm_write_special(XiveNVT *nvt, hwaddr offset,
                                        uint64_t value, unsigned size)
{
    /* TODO: support TM_SPC_SET_OS_PENDING */

    /* TODO: support TM_SPC_ACK_OS_EL */
}

static void xive_tm_os_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    XiveNVT *nvt = XIVE_NVT(cpu->intc);
    int i;

    if (offset >= TM_SPC_ACK_EBB) {
        xive_tm_write_special(nvt, offset, value, size);
        return;
    }

    if (TM_RING(offset) != TM_QW1_OS) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid access to non-OS ring @%"
                      HWADDR_PRIx"\n", offset);
        return;
    }

    switch (size) {
    case 1:
        if (offset == TM_QW1_OS + TM_CPPR) {
            xive_nvt_set_cppr(nvt, value & 0xff);
        }
        break;
    case 4:
    case 8:
        for (i = 0; i < size; i++) {
            if (!xive_tm_is_readonly(offset + i)) {
                nvt->regs[offset + i] = (value >> (8 * (size - i - 1))) & 0xff;
            }
        }
        break;
    default:
        g_assert_not_reached();
    }
}

const MemoryRegionOps xive_tm_os_ops = {
    .read = xive_tm_os_read,
    .write = xive_tm_os_write,
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
 * User Thread Interrupt Management Area MMIO
 */

static uint64_t xive_tm_user_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "XIVE: invalid access to User TIMA @%"
                  HWADDR_PRIx"\n", offset);
    return -1;
}

static void xive_tm_user_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "XIVE: invalid access to User TIMA @%"
                  HWADDR_PRIx"\n", offset);
}


const MemoryRegionOps xive_tm_user_ops = {
    .read = xive_tm_user_read,
    .write = xive_tm_user_write,
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

static char *xive_nvt_ring_print(uint8_t *ring)
{
    uint32_t w2 = be32_to_cpu(*((uint32_t *) &ring[TM_WORD2]));

    return g_strdup_printf("%02x  %02x   %02x  %02x    %02x   "
                   "%02x  %02x  %02x   %08x",
                   ring[TM_NSR], ring[TM_CPPR], ring[TM_IPB], ring[TM_LSMFB],
                   ring[TM_ACK_CNT], ring[TM_INC], ring[TM_AGE], ring[TM_PIPR],
                   w2);
}

void xive_nvt_pic_print_info(XiveNVT *nvt, Monitor *mon)
{
    int cpu_index = nvt->cs ? nvt->cs->cpu_index : -1;
    char *s;

    monitor_printf(mon, "CPU[%04x]: QW    NSR CPPR IPB LSMFB ACK# INC AGE PIPR"
                   " W2\n", cpu_index);

    s = xive_nvt_ring_print(&nvt->regs[TM_QW1_OS]);
    monitor_printf(mon, "CPU[%04x]: OS    %s\n", cpu_index, s);
    g_free(s);
    s = xive_nvt_ring_print(&nvt->regs[TM_QW0_USER]);
    monitor_printf(mon, "CPU[%04x]: USER  %s\n", cpu_index, s);
    g_free(s);
}

static void xive_nvt_reset(void *dev)
{
    XiveNVT *nvt = XIVE_NVT(dev);

    memset(nvt->regs, 0, sizeof(nvt->regs));
}

static void xive_nvt_realize(DeviceState *dev, Error **errp)
{
    XiveNVT *nvt = XIVE_NVT(dev);
    PowerPCCPU *cpu;
    CPUPPCState *env;
    Object *obj;
    Error *err = NULL;

    obj = object_property_get_link(OBJECT(dev), ICP_PROP_CPU, &err);
    if (!obj) {
        error_propagate(errp, err);
        error_prepend(errp, "required link '" ICP_PROP_CPU "' not found: ");
        return;
    }

    cpu = POWERPC_CPU(obj);
    nvt->cs = CPU(obj);

    env = &cpu->env;
    switch (PPC_INPUT(env)) {
    case PPC_FLAGS_INPUT_POWER7:
        nvt->output = env->irq_inputs[POWER7_INPUT_INT];
        break;

    default:
        error_setg(errp, "XIVE interrupt controller does not support "
                   "this CPU bus model");
        return;
    }

    qemu_register_reset(xive_nvt_reset, dev);
}

static void xive_nvt_unrealize(DeviceState *dev, Error **errp)
{
    qemu_unregister_reset(xive_nvt_reset, dev);
}

static void xive_nvt_init(Object *obj)
{
    XiveNVT *nvt = XIVE_NVT(obj);

    nvt->ring_os = &nvt->regs[TM_QW1_OS];
}

static const VMStateDescription vmstate_xive_nvt = {
    .name = TYPE_XIVE_NVT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(regs, XiveNVT),
        VMSTATE_END_OF_LIST()
    },
};

static void xive_nvt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xive_nvt_realize;
    dc->unrealize = xive_nvt_unrealize;
    dc->desc = "XIVE Interrupt Presenter";
    dc->vmsd = &vmstate_xive_nvt;
}

static const TypeInfo xive_nvt_info = {
    .name          = TYPE_XIVE_NVT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XiveNVT),
    .instance_init = xive_nvt_init,
    .class_init    = xive_nvt_class_init,
};

/*
 * XIVE Fabric
 */

XiveIVE *xive_fabric_get_ive(XiveFabric *xf, uint32_t lisn)
{
    XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xf);

    return xfc->get_ive(xf, lisn);
}

XiveNVT *xive_fabric_get_nvt(XiveFabric *xf, uint32_t server)
{
    XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xf);

    return xfc->get_nvt(xf, server);
}

static void xive_fabric_route(XiveFabric *xf, int lisn)
{

}

static const TypeInfo xive_fabric_info = {
    .name = TYPE_XIVE_FABRIC,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(XiveFabricClass),
};

/*
 * XIVE Interrupt Source
 */

uint8_t xive_source_pq_get(XiveSource *xsrc, uint32_t srcno)
{
    uint32_t byte = srcno / 4;
    uint32_t bit  = (srcno % 4) * 2;

    assert(byte < xsrc->sbe_size);

    return (xsrc->sbe[byte] >> bit) & 0x3;
}

uint8_t xive_source_pq_set(XiveSource *xsrc, uint32_t srcno, uint8_t pq)
{
    uint32_t byte = srcno / 4;
    uint32_t bit  = (srcno % 4) * 2;
    uint8_t old, new;

    assert(byte < xsrc->sbe_size);

    old = xsrc->sbe[byte];

    new = xsrc->sbe[byte] & ~(0x3 << bit);
    new |= (pq & 0x3) << bit;

    xsrc->sbe[byte] = new;

    return (old >> bit) & 0x3;
}

static bool xive_source_pq_eoi(XiveSource *xsrc, uint32_t srcno)
{
    uint8_t old_pq = xive_source_pq_get(xsrc, srcno);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_PENDING:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_QUEUED:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_OFF:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

/*
 * Returns whether the event notification should be forwarded.
 */
static bool xive_source_pq_trigger(XiveSource *xsrc, uint32_t srcno)
{
    uint8_t old_pq = xive_source_pq_get(xsrc, srcno);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_PENDING:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_QUEUED);
        return false;
    case XIVE_ESB_QUEUED:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_QUEUED);
        return false;
    case XIVE_ESB_OFF:
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

/*
 * Forward the source event notification to the associated XiveFabric,
 * the device owning the sources, or perform the routing if the device
 * is the interrupt controller.
 */
static void xive_source_notify(XiveSource *xsrc, int srcno)
{

    XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xsrc->xive);

    if (xfc->notify) {
        xfc->notify(xsrc->xive, srcno + xsrc->offset);
    } else {
        xive_fabric_route(xsrc->xive, srcno + xsrc->offset);
    }
}

/*
 * LSI interrupt sources use the P bit and a custom assertion flag
 */
static bool xive_source_lsi_trigger(XiveSource *xsrc, uint32_t srcno)
{
    uint8_t old_pq = xive_source_pq_get(xsrc, srcno);

    if  (old_pq == XIVE_ESB_RESET &&
         xsrc->status[srcno] & XIVE_STATUS_ASSERTED) {
        xive_source_pq_set(xsrc, srcno, XIVE_ESB_PENDING);
        return true;
    }
    return false;
}

/* In a two pages ESB MMIO setting, even page is the trigger page, odd
 * page is for management */
static inline bool xive_source_is_trigger_page(hwaddr addr)
{
    return !((addr >> 16) & 1);
}

static uint64_t xive_source_esb_read(void *opaque, hwaddr addr, unsigned size)
{
    XiveSource *xsrc = XIVE_SOURCE(opaque);
    uint32_t offset = addr & 0xF00;
    uint32_t srcno = addr >> xsrc->esb_shift;
    uint64_t ret = -1;

    if (xive_source_esb_2page(xsrc) && xive_source_is_trigger_page(addr)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: invalid load on IRQ %d trigger page at "
                      "0x%"HWADDR_PRIx"\n", srcno, addr);
        return -1;
    }

    switch (offset) {
    case XIVE_ESB_LOAD_EOI:
        /*
         * Load EOI is not the default source setting under QEMU, but
         * this is what HW uses currently.
         */
        ret = xive_source_pq_eoi(xsrc, srcno);

        /* If the LSI source is still asserted, forward a new source
         * event notification */
        if (xive_source_irq_is_lsi(xsrc, srcno)) {
            if (xive_source_lsi_trigger(xsrc, srcno)) {
                xive_source_notify(xsrc, srcno);
            }
        }
        break;

    case XIVE_ESB_GET:
        ret = xive_source_pq_get(xsrc, srcno);
        break;

    case XIVE_ESB_SET_PQ_00:
    case XIVE_ESB_SET_PQ_01:
    case XIVE_ESB_SET_PQ_10:
    case XIVE_ESB_SET_PQ_11:
        ret = xive_source_pq_set(xsrc, srcno, (offset >> 8) & 0x3);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB addr %d\n", offset);
    }

    return ret;
}

static void xive_source_esb_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    XiveSource *xsrc = XIVE_SOURCE(opaque);
    uint32_t offset = addr & 0xF00;
    uint32_t srcno = addr >> xsrc->esb_shift;
    bool notify = false;

    switch (offset) {
    case 0:
        notify = xive_source_pq_trigger(xsrc, srcno);
        break;

    case XIVE_ESB_STORE_EOI:
        if (xive_source_is_trigger_page(addr)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: invalid store on IRQ %d trigger page at "
                          "0x%"HWADDR_PRIx"\n", srcno, addr);
            return;
        }

        if (!(xsrc->esb_flags & XIVE_SRC_STORE_EOI)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: invalid Store EOI for IRQ %d\n", srcno);
            return;
        }

        /* If the Q bit is set, we should forward a new source event
         * notification
         */
        notify = xive_source_pq_eoi(xsrc, srcno);

        /* LSI sources do not set the Q bit but they can still be
         * asserted, in which case we should forward a new source
         * event notification
         */
        if (xive_source_irq_is_lsi(xsrc, srcno)) {
            notify = xive_source_lsi_trigger(xsrc, srcno);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB write addr %d\n",
                      offset);
        return;
    }

    /* Forward the source event notification for routing */
    if (notify) {
        xive_source_notify(xsrc, srcno);
    }
}

static const MemoryRegionOps xive_source_esb_ops = {
    .read = xive_source_esb_read,
    .write = xive_source_esb_write,
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

static void xive_source_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = XIVE_SOURCE(opaque);
    bool notify = false;

    if (xive_source_irq_is_lsi(xsrc, srcno)) {
        if (val) {
            xsrc->status[srcno] |= XIVE_STATUS_ASSERTED;
        } else {
            xsrc->status[srcno] &= ~XIVE_STATUS_ASSERTED;
        }
        notify = xive_source_lsi_trigger(xsrc, srcno);
    } else {
        if (val) {
            notify = xive_source_pq_trigger(xsrc, srcno);
        }
    }

    /* Forward the source event notification for routing */
    if (notify) {
        xive_source_notify(xsrc, srcno);
    }
}

void xive_source_pic_print_info(XiveSource *xsrc, Monitor *mon)
{
    int i;

    monitor_printf(mon, "XIVE Source %6x ..%6x\n",
                   xsrc->offset, xsrc->offset + xsrc->nr_irqs - 1);
    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq = xive_source_pq_get(xsrc, i);

        if (pq == XIVE_ESB_OFF) {
            continue;
        }

        monitor_printf(mon, "  %4x %s %c%c\n", i + xsrc->offset,
                       xive_source_irq_is_lsi(xsrc, i) ? "LSI" : "MSI",
                       pq & XIVE_ESB_VAL_P ? 'P' : '-',
                       pq & XIVE_ESB_VAL_Q ? 'Q' : '-');
    }
}

static void xive_source_reset(DeviceState *dev)
{
    XiveSource *xsrc = XIVE_SOURCE(dev);
    int i;

    /* Keep the IRQ type */
    for (i = 0; i < xsrc->nr_irqs; i++) {
        xsrc->status[i] &= ~XIVE_STATUS_ASSERTED;
    }

    /* SBEs are initialized to 0b01 which corresponds to "ints off" */
    memset(xsrc->sbe, 0x55, xsrc->sbe_size);
}

static void xive_source_realize(DeviceState *dev, Error **errp)
{
    XiveSource *xsrc = XIVE_SOURCE(dev);
    Object *obj;
    Error *local_err = NULL;

    obj = object_property_get_link(OBJECT(dev), "xive", &local_err);
    if (!obj) {
        error_propagate(errp, local_err);
        error_prepend(errp, "required link 'xive' not found: ");
        return;
    }

    xsrc->xive = XIVE_FABRIC(obj);

    if (!xsrc->nr_irqs) {
        error_setg(errp, "Number of interrupt needs to be greater than 0");
        return;
    }

    if (xsrc->esb_shift != XIVE_ESB_4K &&
        xsrc->esb_shift != XIVE_ESB_4K_2PAGE &&
        xsrc->esb_shift != XIVE_ESB_64K &&
        xsrc->esb_shift != XIVE_ESB_64K_2PAGE) {
        error_setg(errp, "Invalid ESB shift setting");
        return;
    }

    xsrc->qirqs = qemu_allocate_irqs(xive_source_set_irq, xsrc,
                                     xsrc->nr_irqs);
    xsrc->status = g_malloc0(xsrc->nr_irqs);

    /* Allocate the SBEs (State Bit Entry). 2 bits, so 4 entries per byte */
    xsrc->sbe_size = DIV_ROUND_UP(xsrc->nr_irqs, 4);
    xsrc->sbe = g_malloc0(xsrc->sbe_size);

    /* TODO: H_INT_ESB support, which removing the ESB MMIOs */

    memory_region_init_io(&xsrc->esb_mmio, OBJECT(xsrc),
                          &xive_source_esb_ops, xsrc, "xive.esb",
                          (1ull << xsrc->esb_shift) * xsrc->nr_irqs);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xsrc->esb_mmio);
}

static const VMStateDescription vmstate_xive_source = {
    .name = TYPE_XIVE_SOURCE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(nr_irqs, XiveSource, NULL),
        VMSTATE_VBUFFER_UINT32(sbe, XiveSource, 1, NULL, sbe_size),
        VMSTATE_END_OF_LIST()
    },
};

/*
 * The default XIVE interrupt source setting for ESB MMIO is two 64k
 * pages without Store EOI. This is in sync with KVM.
 */
static Property xive_source_properties[] = {
    DEFINE_PROP_UINT64("flags", XiveSource, esb_flags, 0),
    DEFINE_PROP_UINT32("nr-irqs", XiveSource, nr_irqs, 0),
    DEFINE_PROP_UINT64("bar", XiveSource, esb_base, 0),
    DEFINE_PROP_UINT32("shift", XiveSource, esb_shift, XIVE_ESB_64K_2PAGE),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_source_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xive_source_realize;
    dc->reset = xive_source_reset;
    dc->props = xive_source_properties;
    dc->desc = "XIVE interrupt source";
    dc->vmsd = &vmstate_xive_source;
}

static const TypeInfo xive_source_info = {
    .name          = TYPE_XIVE_SOURCE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XiveSource),
    .class_init    = xive_source_class_init,
};

static void xive_register_types(void)
{
    type_register_static(&xive_source_info);
    type_register_static(&xive_fabric_info);
    type_register_static(&xive_nvt_info);
}

type_init(xive_register_types)
