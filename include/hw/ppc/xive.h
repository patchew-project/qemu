/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_XIVE_H
#define PPC_XIVE_H

#include "hw/sysbus.h"
#include "hw/ppc/xive_regs.h"

typedef struct XiveFabric XiveFabric;

/*
 * XIVE MMIO regions
 */

#define XIVE_VC_BASE   0x0006010000000000ull
#define XIVE_TM_BASE   0x0006030203180000ull

/*
 * XIVE Interrupt Source
 */

#define TYPE_XIVE_SOURCE "xive-source"
#define XIVE_SOURCE(obj) OBJECT_CHECK(XiveSource, (obj), TYPE_XIVE_SOURCE)

/*
 * XIVE Source Interrupt source characteristics, which define how the
 * ESB are controlled.
 */
#define XIVE_SRC_H_INT_ESB     0x1 /* ESB managed with hcall H_INT_ESB */
#define XIVE_SRC_STORE_EOI     0x4 /* Store EOI supported */

typedef struct XiveSource {
    SysBusDevice parent;

    /* IRQs */
    uint32_t     nr_irqs;
    uint32_t     offset;
    qemu_irq     *qirqs;
#define XIVE_STATUS_LSI         0x1
#define XIVE_STATUS_ASSERTED    0x2
    uint8_t      *status;

    /* PQ bits */
    uint8_t      *sbe;
    uint32_t     sbe_size;

    /* ESB memory region */
    uint64_t     esb_flags;
    hwaddr       esb_base;
    uint32_t     esb_shift;
    MemoryRegion esb_mmio;

    XiveFabric   *xive;
} XiveSource;

/*
 * ESB MMIO setting. Can be one page, for both source triggering and
 * source management, or two different pages. See below for magic
 * values.
 */
#define XIVE_ESB_4K          12 /* PSI HB */
#define XIVE_ESB_4K_2PAGE    17
#define XIVE_ESB_64K         16
#define XIVE_ESB_64K_2PAGE   17

static inline bool xive_source_esb_2page(XiveSource *xsrc)
{
    return xsrc->esb_shift == XIVE_ESB_64K_2PAGE;
}

static inline hwaddr xive_source_esb_base(XiveSource *xsrc, uint32_t srcno)
{
    assert(srcno < xsrc->nr_irqs);
    return xsrc->esb_base + (1ull << xsrc->esb_shift) * srcno;
}

/* The trigger page is always the first/even page */
#define xive_source_esb_trigger xive_source_esb_base

/* In a two pages ESB MMIO setting, the odd page is for management */
static inline hwaddr xive_source_esb_mgmt(XiveSource *xsrc, int srcno)
{
    hwaddr addr = xive_source_esb_base(xsrc, srcno);

    if (xive_source_esb_2page(xsrc)) {
        addr += (1 << (xsrc->esb_shift - 1));
    }

    return addr;
}

/*
 * Each interrupt source has a 2-bit state machine called ESB which
 * can be controlled by MMIO. It's made of 2 bits, P and Q. P
 * indicates that an interrupt is pending (has been sent to a queue
 * and is waiting for an EOI). Q indicates that the interrupt has been
 * triggered while pending.
 *
 * This acts as a coalescing mechanism in order to guarantee
 * that a given interrupt only occurs at most once in a queue.
 *
 * When doing an EOI, the Q bit will indicate if the interrupt
 * needs to be re-triggered.
 */
#define XIVE_ESB_VAL_P        0x2
#define XIVE_ESB_VAL_Q        0x1

#define XIVE_ESB_RESET        0x0
#define XIVE_ESB_PENDING      XIVE_ESB_VAL_P
#define XIVE_ESB_QUEUED       (XIVE_ESB_VAL_P | XIVE_ESB_VAL_Q)
#define XIVE_ESB_OFF          XIVE_ESB_VAL_Q

/*
 * "magic" Event State Buffer (ESB) MMIO offsets.
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
#define XIVE_ESB_STORE_EOI      0x400 /* Store */
#define XIVE_ESB_LOAD_EOI       0x000 /* Load */
#define XIVE_ESB_GET            0x800 /* Load */
#define XIVE_ESB_SET_PQ_00      0xc00 /* Load */
#define XIVE_ESB_SET_PQ_01      0xd00 /* Load */
#define XIVE_ESB_SET_PQ_10      0xe00 /* Load */
#define XIVE_ESB_SET_PQ_11      0xf00 /* Load */

uint8_t xive_source_pq_get(XiveSource *xsrc, uint32_t srcno);
uint8_t xive_source_pq_set(XiveSource *xsrc, uint32_t srcno, uint8_t pq);

void xive_source_pic_print_info(XiveSource *xsrc, Monitor *mon);

static inline bool xive_source_irq_is_lsi(XiveSource *xsrc, uint32_t srcno)
{
    assert(srcno < xsrc->nr_irqs);
    return xsrc->status[srcno] & XIVE_STATUS_LSI;
}

static inline void xive_source_irq_set(XiveSource *xsrc, uint32_t srcno,
                                       bool lsi)
{
    assert(srcno < xsrc->nr_irqs);
    xsrc->status[srcno] |= lsi ? XIVE_STATUS_LSI : 0;
}

/*
 * XIVE Interrupt Presenter
 */

#define TYPE_XIVE_NVT "xive-nvt"
#define XIVE_NVT(obj) OBJECT_CHECK(XiveNVT, (obj), TYPE_XIVE_NVT)

#define TM_RING_COUNT           4
#define TM_RING_SIZE            0x10

typedef struct XiveNVT {
    DeviceState parent_obj;

    CPUState  *cs;
    qemu_irq  output;

    /* Thread interrupt Management (TM) registers */
    uint8_t   regs[TM_RING_COUNT * TM_RING_SIZE];

    /* Shortcuts to rings */
    uint8_t   *ring_os;

    XiveEQ    eqt[XIVE_PRIORITY_MAX + 1];
} XiveNVT;

extern const MemoryRegionOps xive_tm_user_ops;
extern const MemoryRegionOps xive_tm_os_ops;

void xive_nvt_pic_print_info(XiveNVT *nvt, Monitor *mon);
XiveEQ *xive_nvt_eq_get(XiveNVT *nvt, uint8_t priority);

void xive_eq_reset(XiveEQ *eq);
void xive_eq_pic_print_info(XiveEQ *eq, Monitor *mon);

/*
 * XIVE Fabric
 */

typedef struct XiveFabric {
    Object parent;
} XiveFabric;

#define TYPE_XIVE_FABRIC "xive-fabric"
#define XIVE_FABRIC(obj)                                     \
    OBJECT_CHECK(XiveFabric, (obj), TYPE_XIVE_FABRIC)
#define XIVE_FABRIC_CLASS(klass)                                     \
    OBJECT_CLASS_CHECK(XiveFabricClass, (klass), TYPE_XIVE_FABRIC)
#define XIVE_FABRIC_GET_CLASS(obj)                                   \
    OBJECT_GET_CLASS(XiveFabricClass, (obj), TYPE_XIVE_FABRIC)

typedef struct XiveFabricClass {
    InterfaceClass parent;
    void (*notify)(XiveFabric *xf, uint32_t lisn);

    XiveIVE *(*get_ive)(XiveFabric *xf, uint32_t lisn);
    XiveNVT *(*get_nvt)(XiveFabric *xf, uint32_t server);
    XiveEQ  *(*get_eq)(XiveFabric *xf, uint32_t eq_idx);
} XiveFabricClass;

XiveIVE *xive_fabric_get_ive(XiveFabric *xf, uint32_t lisn);
XiveNVT *xive_fabric_get_nvt(XiveFabric *xf, uint32_t server);
XiveEQ  *xive_fabric_get_eq(XiveFabric *xf, uint32_t eq_idx);

#endif /* PPC_XIVE_H */
