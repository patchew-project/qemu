/*
 * QEMU model of Xilinx AXI-CDMA block.
 *
 * Copyright (c) 2022 Frank Chang <frank.chang@sifive.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "sysemu/dma.h"
#include "hw/dma/xilinx_axicdma.h"

#define R_CDMACR                    0x00
#define R_CDMASR                    0x04
#define R_CURDESC                   0x08
#define R_CURDESC_MSB               0x0c
#define R_TAILDESC                  0x10
#define R_TAILDESC_MSB              0x14
#define R_SA                        0x18
#define R_SA_MSB                    0x1c
#define R_DA                        0x20
#define R_DA_MSB                    0x24
#define R_BTT                       0x28

#define R_MAX                       0x30

/* CDMACR */
#define CDMACR_TAIL_PNTR_EN         BIT(1)
#define CDMACR_RESET                BIT(2)
#define CDMACR_SGMODE               BIT(3)
#define CDMACR_KEY_HOLE_READ        BIT(4)
#define CDMACR_KEY_HOLE_WRITE       BIT(5)
#define CDMACR_CYCLIC_BD_ENABLE     BIT(6)
#define CDMACR_IOC_IRQ_EN           BIT(12)
#define CDMACR_DLY_IRQ_EN           BIT(13)
#define CDMACR_ERR_IRQ_EN           BIT(14)

#define CDMACR_MASK                 0xffff707c

/* TailPntrEn = 1,  IRQThreshold = 1. */
#define CDMACR_DEFAULT              0x10002

/* CDMASR */
#define CDMASR_IDLE                 BIT(1)
#define CDMASR_SG_INCLUD            BIT(3)
#define CDMASR_DMA_INT_ERR          BIT(4)
#define CDMASR_DMA_SLV_ERR          BIT(5)
#define CDMASR_DMA_DEC_ERR          BIT(6)
#define CDMASR_SG_INT_ERR           BIT(8)
#define CDMASR_SG_SLV_ERR           BIT(9)
#define CDMASR_SG_DEC_ERR           BIT(10)
#define CDMASR_IOC_IRQ              BIT(12)
#define CDMASR_DLY_IRQ              BIT(13)
#define CDMASR_ERR_IRQ              BIT(14)

#define CDMASR_IRQ_THRES_SHIFT      16
#define CDMASR_IRQ_THRES_WIDTH      8
#define CDMASR_IRQ_DELAY_SHIFT      24
#define CDMASR_IRQ_DELAY_WIDTH      8

#define CDMASR_IRQ_MASK             (CDMASR_IOC_IRQ | \
                                     CDMASR_DLY_IRQ | \
                                     CDMASR_ERR_IRQ)

/* Idle = 1, SGIncld = 1, IRQThresholdSts = 1. */
#define CDMASR_DEFAULT              0x1000a

#define CURDESC_MASK                0xffffffc0

#define TAILDESC_MASK               0xffffffc0

#define BTT_MAX_SIZE                (1UL << 26)
#define BTT_MASK                    (BTT_MAX_SIZE - 1)

/* SDesc - Status */
#define SDEC_STATUS_DMA_INT_ERR     BIT(28)
#define SDEC_STATUS_DMA_SLV_ERR     BIT(29)
#define SDEC_STATUS_DMA_DEC_ERR     BIT(30)
#define SDEC_STATUS_DMA_CMPLT       BIT(31)


static void axicdma_set_dma_err(XilinxAXICDMA *s, uint32_t err);
static void axicdma_set_sg_dma_err(XilinxAXICDMA *s, uint32_t err, hwaddr addr);
static void axicdma_set_sg_err(XilinxAXICDMA *s, uint32_t err);

static void axicdma_update_irq(XilinxAXICDMA *s)
{
    uint32_t enable, pending;

    enable = s->control & CDMASR_IRQ_MASK;
    pending = s->status & CDMASR_IRQ_MASK;

    qemu_set_irq(s->irq, !!(enable & pending));
}

static void axicdma_set_irq(XilinxAXICDMA *s, uint32_t irq, bool level)
{
    g_assert(irq == CDMASR_IOC_IRQ ||
             irq == CDMASR_DLY_IRQ ||
             irq == CDMASR_ERR_IRQ);

    if (level) {
        s->status |= irq;
    } else {
        s->status &= ~irq;
    }

    axicdma_update_irq(s);
}

static void axicdma_reload_complete_cnt(XilinxAXICDMA *s)
{
    s->complete_cnt = extract32(s->control, CDMASR_IRQ_THRES_SHIFT,
                                CDMASR_IRQ_THRES_WIDTH);
}

static void timer_hit(void *opaque)
{
    XilinxAXICDMA *s = XILINX_AXI_CDMA(opaque);

    axicdma_set_irq(s, CDMASR_DLY_IRQ, true);
    axicdma_reload_complete_cnt(s);
}

static bool sdesc_load(XilinxAXICDMA *s, hwaddr addr)
{
    SDesc *d = &s->sdesc;
    MemTxResult ret;

    ret = address_space_read(s->as, addr, MEMTXATTRS_UNSPECIFIED,
                             d, sizeof(SDesc));
    if (ret != MEMTX_OK) {
        axicdma_set_sg_err(s, CDMASR_SG_DEC_ERR);
        return false;
    }

    /* Convert from LE into host endianness. */
    d->nxtdesc = le64_to_cpu(d->nxtdesc);
    d->src = le64_to_cpu(d->src);
    d->dest = le64_to_cpu(d->dest);
    d->control = le32_to_cpu(d->control);
    d->status = le32_to_cpu(d->status);

    return true;
}

static bool sdesc_store(XilinxAXICDMA *s, hwaddr addr)
{
    SDesc *d = &s->sdesc;
    MemTxResult ret;

    /* Convert from host endianness into LE. */
    d->nxtdesc = cpu_to_le64(d->nxtdesc);
    d->src = cpu_to_le64(d->src);
    d->dest = cpu_to_le64(d->dest);
    d->control = cpu_to_le32(d->control);
    d->status = cpu_to_le32(d->status);

    ret = address_space_write(s->as, addr, MEMTXATTRS_UNSPECIFIED,
                              d, sizeof(SDesc));
    if (ret != MEMTX_OK) {
        axicdma_set_sg_err(s, CDMASR_SG_DEC_ERR);
        return false;
    }

    return true;
}

static void sdesc_complete(XilinxAXICDMA *s)
{
    uint32_t irq_delay = extract32(s->control, CDMASR_IRQ_DELAY_SHIFT,
                                   CDMASR_IRQ_DELAY_WIDTH);

    if (irq_delay) {
        /* Restart the delayed timer. */
        ptimer_transaction_begin(s->ptimer);
        ptimer_stop(s->ptimer);
        ptimer_set_count(s->ptimer, irq_delay);
        ptimer_run(s->ptimer, 1);
        ptimer_transaction_commit(s->ptimer);
    }

    s->complete_cnt--;

    if (s->complete_cnt == 0) {
        /* Raise the IOC irq. */
        axicdma_set_irq(s, CDMASR_IOC_IRQ, true);
        axicdma_reload_complete_cnt(s);
    }
}

static inline bool axicdma_sgmode(XilinxAXICDMA *s)
{
    return !!(s->control & CDMACR_SGMODE);
}

static void axicdma_set_dma_err(XilinxAXICDMA *s, uint32_t err)
{
    g_assert(err == CDMASR_DMA_INT_ERR ||
             err == CDMASR_DMA_SLV_ERR ||
             err == CDMASR_DMA_DEC_ERR);

    s->status |= err;
    axicdma_set_irq(s, CDMASR_ERR_IRQ, true);
}

static void axicdma_set_sg_dma_err(XilinxAXICDMA *s, uint32_t err, hwaddr addr)
{
    g_assert(axicdma_sgmode(s));

    axicdma_set_dma_err(s, err);

    /*
     * There are 24-bit shift between
     * SDesc status bit and CDMACR.DMA_[INT|SLV|DEC]_ERR bit.
     */
    s->sdesc.status |= (err << 24);
    sdesc_store(s, addr);
}

static void axicdma_set_sg_err(XilinxAXICDMA *s, uint32_t err)
{
    g_assert(err == CDMASR_SG_INT_ERR ||
             err == CDMASR_SG_SLV_ERR ||
             err == CDMASR_SG_DEC_ERR);

    s->status |= err;
    axicdma_set_irq(s, CDMASR_ERR_IRQ, true);
}

static bool axicdma_perform_dma(XilinxAXICDMA *s, hwaddr src, hwaddr dest,
                                uint32_t btt)
{
    uint32_t r_off = 0, w_off = 0;
    uint32_t len;
    MemTxResult ret;

    while (btt > 0) {
        len = MIN(btt, CDMA_BUF_SIZE);

        ret = dma_memory_read(s->as, src + r_off, s->buf + r_off, len,
                              MEMTXATTRS_UNSPECIFIED);
        if (ret != MEMTX_OK) {
            return false;
        }

        ret = dma_memory_write(s->as, dest + w_off, s->buf + w_off, len,
                               MEMTXATTRS_UNSPECIFIED);
        if (ret != MEMTX_OK) {
            return false;
        }

        btt -= len;

        if (!(s->control & CDMACR_KEY_HOLE_READ)) {
            r_off += len;
        }

        if (!(s->control & CDMACR_KEY_HOLE_WRITE)) {
            w_off += len;
        }
    }

    return true;
}

static void axicdma_run_simple(XilinxAXICDMA *s)
{
    if (!s->btt) {
        /* Value of zero BTT is not allowed. */
        axicdma_set_dma_err(s, CDMASR_DMA_INT_ERR);
        return;
    }

    if (!axicdma_perform_dma(s, s->src, s->dest, s->btt)) {
        axicdma_set_dma_err(s, CDMASR_DMA_DEC_ERR);
        return;
    }

    /* Generate IOC interrupt. */
    axicdma_set_irq(s, CDMASR_IOC_IRQ, true);
}

static void axicdma_run_sgmode(XilinxAXICDMA *s)
{
    uint64_t pdesc;
    uint32_t btt;

    while (1) {
        if (!sdesc_load(s, s->curdesc)) {
            break;
        }

        if (s->sdesc.status & SDEC_STATUS_DMA_CMPLT) {
            axicdma_set_sg_err(s, CDMASR_SG_INT_ERR);
            break;
        }

        btt = s->sdesc.control & BTT_MASK;

        if (btt == 0) {
            /* Value of zero BTT is not allowed. */
            axicdma_set_sg_err(s, CDMASR_SG_INT_ERR);
            break;
        }

        if (!axicdma_perform_dma(s, s->sdesc.src, s->sdesc.dest, btt)) {
            axicdma_set_sg_dma_err(s, CDMASR_DMA_DEC_ERR, s->curdesc);
            break;
        }

        /* Update the descriptor. */
        s->sdesc.status |= SDEC_STATUS_DMA_CMPLT;
        sdesc_store(s, s->curdesc);
        sdesc_complete(s);

        /* Advance current descriptor pointer. */
        pdesc = s->curdesc;
        s->curdesc = s->sdesc.nxtdesc;

        if (!(s->control & CDMACR_CYCLIC_BD_ENABLE) &&
            pdesc == s->taildesc) {
            /* Reach the tail descriptor. */
            break;
        }
    }

    /* Stop the delayed timer. */
    ptimer_transaction_begin(s->ptimer);
    ptimer_stop(s->ptimer);
    ptimer_transaction_commit(s->ptimer);
}

static void axicdma_run(XilinxAXICDMA *s)
{
    bool sgmode = axicdma_sgmode(s);

    /* Not idle. */
    s->status &= ~CDMASR_IDLE;

    if (!sgmode) {
        axicdma_run_simple(s);
    } else {
        axicdma_run_sgmode(s);
    }

    /* Idle. */
    s->status |= CDMASR_IDLE;
}

static void axicdma_reset(XilinxAXICDMA *s)
{
    s->control = CDMACR_DEFAULT;
    s->status = CDMASR_DEFAULT;
    s->complete_cnt = 1;
    qemu_irq_lower(s->irq);
}

static void axicdma_write_control(XilinxAXICDMA *s, uint32_t value)
{
    if (value & CDMACR_RESET) {
        axicdma_reset(s);
        return;
    }

    /*
     * The minimum setting for the threshold is 0x01.
     * A write of 0x00 to CDMACR.IRQThreshold has no effect.
     */
    if (!extract32(value, CDMASR_IRQ_THRES_SHIFT, CDMASR_IRQ_THRES_WIDTH)) {
        value = deposit32(value, CDMASR_IRQ_THRES_SHIFT, CDMASR_IRQ_THRES_WIDTH,
                          s->control);
    }

    /*
     * AXI CDMA is built with SG enabled,
     * tail pointer mode is always enabled.
     */
    s->control = (value & CDMACR_MASK) | CDMACR_TAIL_PNTR_EN;

    if (!axicdma_sgmode(s)) {
        /*
         * CDMASR.Dly_Irq, CURDESC_PNTR, TAILDESC_PNTR are cleared
         * when not in SGMode.
         */
        s->status &= ~CDMASR_DLY_IRQ;
        s->curdesc = 0;
        s->taildesc = 0;
    }

    axicdma_reload_complete_cnt(s);
}

static uint32_t axicdma_read_status(XilinxAXICDMA *s)
{
    uint32_t value = s->status;

    value = deposit32(value, CDMASR_IRQ_THRES_SHIFT,
                      CDMASR_IRQ_THRES_WIDTH, s->complete_cnt);
    value = deposit32(value, CDMASR_IRQ_DELAY_SHIFT,
                      CDMASR_IRQ_DELAY_WIDTH, ptimer_get_count(s->ptimer));

    return value;
}

static void axicdma_write_status(XilinxAXICDMA *s, uint32_t value)
{
    /* Write 1s to clear interrupts. */
    s->status &= ~(value & CDMASR_IRQ_MASK);
    axicdma_update_irq(s);
}

static void axicdma_write_curdesc(XilinxAXICDMA *s, uint64_t value)
{
    /* Should be idle. */
    g_assert(s->status & CDMASR_IDLE);

    if (!axicdma_sgmode(s)) {
        /* This register is cleared if SGMode = 0. */
        return;
    }

    s->curdesc = value & CURDESC_MASK;
}

static void axicdma_write_taildesc(XilinxAXICDMA *s, uint64_t value)
{
    /* Should be idle. */
    g_assert(s->status & CDMASR_IDLE);

    if (!axicdma_sgmode(s)) {
        /* This register is cleared if SGMode = 0. */
        return;
    }

    s->taildesc = value & TAILDESC_MASK;

    /* Kick-off CDMA. */
    axicdma_run(s);
}

static void axicdma_write_btt(XilinxAXICDMA *s, uint64_t value)
{
    s->btt = value & BTT_MASK;

    if (!axicdma_sgmode(s)) {
        /* Kick-off CDMA. */
        axicdma_run(s);
    }
}

static uint32_t axicdma_readl(void *opaque, hwaddr addr, unsigned size)
{
    XilinxAXICDMA *s = opaque;
    uint32_t value = 0;

    if (s->addrwidth <= 32) {
        switch (addr) {
        case R_CURDESC_MSB:
        case R_TAILDESC_MSB:
        case R_SA_MSB:
        case R_DA_MSB:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid 32-bit read to 0x%" HWADDR_PRIX "\n",
                          __func__, addr);
            return value;
        }
    }

    switch (addr) {
    case R_CDMACR:
        value = s->control;
        break;
    case R_CDMASR:
        value = axicdma_read_status(s);
        break;
    case R_CURDESC:
        value = s->curdesc;
        break;
    case R_CURDESC_MSB:
        value = extract64(s->curdesc, 32, 32);
        break;
    case R_TAILDESC:
        value = s->taildesc;
        break;
    case R_TAILDESC_MSB:
        value = extract64(s->taildesc, 32, 32);
        break;
    case R_SA:
        value = s->src;
        break;
    case R_SA_MSB:
        value = extract64(s->src, 32, 32);
        break;
    case R_DA:
        value = s->dest;
        break;
    case R_DA_MSB:
        value = extract64(s->dest, 32, 32);
        break;
    case R_BTT:
        value = s->btt;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid 32-bit read to 0x%" HWADDR_PRIX "\n",
                      __func__, addr);
    }

    return value;
}

static uint32_t axicdma_readq(void *opaque, hwaddr addr, unsigned size)
{
    XilinxAXICDMA *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case R_CDMACR:
        value = s->control;
        break;
    case R_CDMASR:
        value = axicdma_read_status(s);
        break;
    case R_CURDESC:
        value = s->curdesc;
        break;
    case R_TAILDESC:
        value = s->taildesc;
        break;
    case R_SA:
        value = s->src;
        break;
    case R_DA:
        value = s->dest;
        break;
    case R_BTT:
        value = s->btt;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid 64-bit read to 0x%" HWADDR_PRIX "\n",
                      __func__, addr);
    }

    return value;
}

static uint64_t axicdma_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t value = 0;

    switch (size) {
    case 4:
        value = axicdma_readl(opaque, addr, size);
        break;
    case 8:
        value = axicdma_readq(opaque, addr, size);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid read size %u to AXI-CDMA\n",
                      __func__, size);
    }

    return value;
}

static void axicdma_writel(void *opaque, hwaddr addr, uint32_t value,
                           unsigned size)
{
    XilinxAXICDMA *s = opaque;

    if (s->addrwidth <= 32) {
        switch (addr) {
        case R_CURDESC_MSB:
        case R_TAILDESC_MSB:
        case R_SA_MSB:
        case R_DA_MSB:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid 32-bit write to 0x%" HWADDR_PRIX "\n",
                          __func__, addr);
            return;
        }
    }

    switch (addr) {
    case R_CDMACR:
        axicdma_write_control(s, value);
        break;
    case R_CDMASR:
        axicdma_write_status(s, value);
        break;
    case R_CURDESC:
        axicdma_write_curdesc(s, deposit64(s->curdesc, 0, 32, value));
        break;
    case R_CURDESC_MSB:
        axicdma_write_curdesc(s, deposit64(s->curdesc, 32, 32, value));
        break;
    case R_TAILDESC:
        axicdma_write_taildesc(s, deposit64(s->taildesc, 0, 32, value));
        break;
    case R_TAILDESC_MSB:
        axicdma_write_taildesc(s, deposit64(s->taildesc, 32, 32, value));
        break;
    case R_SA:
        s->src = deposit64(s->src, 0, 32, value);
        break;
    case R_SA_MSB:
        s->src = deposit64(s->src, 32, 32, value);
        break;
    case R_DA:
        s->dest = deposit64(s->dest, 0, 32, value);
        break;
    case R_DA_MSB:
        s->dest = deposit64(s->dest, 32, 32, value);
        break;
    case R_BTT:
        axicdma_write_btt(s, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid 32-bit write to 0x%" HWADDR_PRIX "\n",
                      __func__, addr);
    }
}

static void axicdma_writeq(void *opaque, hwaddr addr, uint64_t value,
                           unsigned size)
{
    XilinxAXICDMA *s = opaque;

    switch (addr) {
    case R_CDMACR:
        axicdma_write_control(s, value);
        break;
    case R_CDMASR:
        axicdma_write_status(s, value);
        break;
    case R_CURDESC:
        axicdma_write_curdesc(s, value);
        break;
    case R_TAILDESC:
        axicdma_write_taildesc(s, value);
        break;
    case R_SA:
        s->src = value;
        break;
    case R_DA:
        s->dest = value;
        break;
    case R_BTT:
        axicdma_write_btt(s, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid 64-bit write to 0x%" HWADDR_PRIX "\n",
                      __func__, addr);
    }
}

static void axicdma_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size)
{
    switch (size) {
    case 4:
        axicdma_writel(opaque, addr, value, size);
        break;
    case 8:
        axicdma_writeq(opaque, addr, value, size);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid write size %u to AXI-CDMA\n",
                      __func__, size);
    }
}

static const MemoryRegionOps axicdma_ops = {
    .read = axicdma_read,
    .write = axicdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void xilinx_axicdma_realize(DeviceState *dev, Error **errp)
{
    XilinxAXICDMA *s = XILINX_AXI_CDMA(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &axicdma_ops, s,
                          TYPE_XILINX_AXI_CDMA, R_MAX);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    if (!s->dma_mr || s->dma_mr == get_system_memory()) {
        /* Avoid creating new AddressSpace for system memory. */
        s->as = &address_space_memory;
    } else {
        s->as = g_new0(AddressSpace, 1);
        address_space_init(s->as, s->dma_mr, memory_region_name(s->dma_mr));
    }

    s->ptimer = ptimer_init(timer_hit, s, PTIMER_POLICY_DEFAULT);
    ptimer_transaction_begin(s->ptimer);
    ptimer_set_freq(s->ptimer, s->freqhz);
    ptimer_transaction_commit(s->ptimer);
}

static void xilinx_axicdma_unrealize(DeviceState *dev)
{
    XilinxAXICDMA *s = XILINX_AXI_CDMA(dev);

    if (s->ptimer) {
        ptimer_free(s->ptimer);
    }

    if (s->as && s->dma_mr != get_system_memory()) {
        g_free(s->as);
    }
}

static Property axicdma_properties[] = {
    DEFINE_PROP_UINT32("freqhz", XilinxAXICDMA, freqhz, 50000000),
    DEFINE_PROP_INT32("addrwidth", XilinxAXICDMA, addrwidth, 64),
    DEFINE_PROP_LINK("dma", XilinxAXICDMA, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_axicdma_reset_enter(Object *obj, ResetType type)
{
    axicdma_reset(XILINX_AXI_CDMA(obj));
}

static void axicdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = xilinx_axicdma_realize;
    dc->unrealize = xilinx_axicdma_unrealize;
    device_class_set_props(dc, axicdma_properties);

    rc->phases.enter = xilinx_axicdma_reset_enter;
}

static const TypeInfo axicdma_info = {
    .name          = TYPE_XILINX_AXI_CDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAXICDMA),
    .class_init    = axicdma_class_init,
};

static void xilinx_axicdma_register_types(void)
{
    type_register_static(&axicdma_info);
}

type_init(xilinx_axicdma_register_types)
