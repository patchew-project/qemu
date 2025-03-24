// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU STM32 Direct memory access controller (DMA).
 *
 * This includes STM32F1xxxx, STM32F2xxxx and GD32F30x
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/register.h"
#include "system/dma.h"

#include "trace.h"

#include "hw/dma/stm32_dma.h"

#define STM32_DMA_APERTURE_SIZE 0x400

/* Global interrupt flag */
#define DMA_ISR_GIF    BIT(0)
/* Full transfer finish */
#define DMA_ISR_TCIF  BIT(1)
/* Half transfer finish */
#define DMA_ISR_HTIF  BIT(2)
/* Transfer error */
#define DMA_ISR_TEIF  BIT(3)

/* Interrupt flag register (DMA_ISR) */
REG32(DMA_ISR, 0x00)
    FIELD(DMA_ISR, CHAN0,     0,  4)
    FIELD(DMA_ISR, CHAN1,     4,  4)
    FIELD(DMA_ISR, CHAN2,     8,  4)
    FIELD(DMA_ISR, CHAN3,     12,  4)
    FIELD(DMA_ISR, CHAN4,     16,  4)
    FIELD(DMA_ISR, CHAN5,     20,  4)
    FIELD(DMA_ISR, CHAN6,     24,  4)
    FIELD(DMA_ISR, RSVD,      28,  4)

/* Interrupt flag clear register (DMA_IFCR) */
REG32(DMA_IFCR, 0x04)
    FIELD(DMA_IFCR, CHAN0,     0,  4)
    FIELD(DMA_IFCR, CHAN1,     4,  4)
    FIELD(DMA_IFCR, CHAN2,     8,  4)
    FIELD(DMA_IFCR, CHAN3,     12,  4)
    FIELD(DMA_IFCR, CHAN4,     16,  4)
    FIELD(DMA_IFCR, CHAN5,     20,  4)
    FIELD(DMA_IFCR, CHAN6,     24,  4)
    FIELD(DMA_IFCR, RSVD,      28,  4)

/* Channel x control register (DMA_CCRx) */
/* Address offset: 0x08 + 0x14 * x */
REG32(DMA_CCR, 0x08)
    FIELD(DMA_CCR, EN,      0,  1)
    FIELD(DMA_CCR, TCIE,    1,  1)
    FIELD(DMA_CCR, HTIE,    2,  1)
    FIELD(DMA_CCR, TEIE,    3,  1)

    /* all below RO if chan enabled */
    FIELD(DMA_CCR, DIR,     4,  1)
    FIELD(DMA_CCR, CIRC,    5,  1)
    FIELD(DMA_CCR, PINC,    6,  1)
    FIELD(DMA_CCR, MINC,    7,  1)
    FIELD(DMA_CCR, PSIZE,   8,  2)
    FIELD(DMA_CCR, MSIZE,   10, 2)
    FIELD(DMA_CCR, PL,      12, 2)
    FIELD(DMA_CCR, M2M,     14, 1)
    FIELD(DMA_CCR, RSVD,    15, 17)

#define DMA_CCR_RO_MASK   \
    (R_DMA_CCR_DIR_MASK \
    | R_DMA_CCR_CIRC_MASK \
    | R_DMA_CCR_PINC_MASK \
    | R_DMA_CCR_MINC_MASK \
    | R_DMA_CCR_PSIZE_MASK \
    | R_DMA_CCR_MSIZE_MASK \
    | R_DMA_CCR_PL_MASK \
    | R_DMA_CCR_M2M_MASK)

/* Channel x counter register (DMA_CNDTRx) */
/* Address offset: 0x0C + 0x14 * x */
REG32(DMA_CNDTR, 0x0c)
    FIELD(DMA_CNDTR, NDT,      0,  16)
    FIELD(DMA_CNDTR, RSVD,     16, 16)

/* Channel x peripheral base address register (DMA_CPARx) */
/* Address offset: 0x10 + 0x14 * x */
REG32(DMA_CPAR, 0x10)
    FIELD(DMA_CPAR, PA,      0,  32)

/* Channel x memory base address register (DMA_CMARx) */
/* 0x14 + 0x14 * x */
REG32(DMA_CMAR, 0x14)
    FIELD(DMA_CMAR, MA,      0,  32)

#define A_DMA_CCR0     0x08
#define A_DMA_CMAR7    0xa0

static void stm32_dma_chan_update_intr(STM32DmaState *s, uint8_t idx)
{
    if (extract32(s->intf, idx * 4, 4)) {
        /* set GIFCx */
        set_bit32(idx * 4, &s->intf);
        qemu_irq_raise(s->output[idx]);
    }
}

static MemTxResult stm32_dma_transfer_one(STM32DmaState *s, uint8_t idx)
{
    STM32DmaChannel *chan = &s->chan[idx];
    uint8_t pwidth = 1 << FIELD_EX32(chan->chctl, DMA_CCR, PSIZE);
    uint8_t mwidth = 1 << FIELD_EX32(chan->chctl, DMA_CCR, MSIZE);
    hwaddr paddr, maddr;
    MemTxResult res = MEMTX_OK;
    uint32_t data = 0;

    paddr = chan->chpaddr;
    if (FIELD_EX32(chan->chctl, DMA_CCR, PINC)) {
        /* increment algorithm enabled */
        uint32_t incr = chan->chcnt_shadow - chan->chcnt;

        paddr += pwidth * incr;
    }

    maddr = chan->chmaddr;
    if (FIELD_EX32(chan->chctl, DMA_CCR, MINC)) {
        /* increment algorithm enabled */
        uint32_t incr = chan->chcnt_shadow - chan->chcnt;

        maddr += mwidth * incr;
    }

    /* issue transaction */
    if (FIELD_EX32(chan->chctl, DMA_CCR, DIR)) {
        /* FROM memory TO peripheral */
        res = dma_memory_read(&address_space_memory, maddr, &data,
                              pwidth, MEMTXATTRS_UNSPECIFIED);
        if (res) {
            goto fail_rw;
        }

        res = dma_memory_write(&address_space_memory, paddr, &data,
                               mwidth, MEMTXATTRS_UNSPECIFIED);
        if (res) {
            goto fail_rw;
        }

        trace_stm32_dma_transfer(idx, maddr, mwidth,
                                 paddr, pwidth, data);
    } else {
        /* FROM peripheral TO memory */
        res = dma_memory_read(&address_space_memory, paddr, &data,
                              pwidth, MEMTXATTRS_UNSPECIFIED);
        if (res) {
            goto fail_rw;
        }

        res = dma_memory_write(&address_space_memory, maddr, &data,
                               mwidth, MEMTXATTRS_UNSPECIFIED);
        if (res) {
            goto fail_rw;
        }

        trace_stm32_dma_transfer(idx, paddr, pwidth,
                                 maddr, mwidth, data);
    }

    if (FIELD_EX32(chan->chctl, DMA_CCR, HTIE)) {
        /* Issue completed half transfer interrupt */
        trace_stm32_dma_raise(idx, "HTIE");
        set_bit(idx * 4 + 2, (unsigned long *)&s->intf);
    }

    return res;

fail_rw:
    if (FIELD_EX32(chan->chctl, DMA_CCR, TEIE)) {
        trace_stm32_dma_raise(idx, "TEIE");
        set_bit(idx * 4 + 3, (unsigned long *)&s->intf);
    }

    trace_stm32_dma_transfer_fail(idx, paddr, maddr);
    return res;
}

static void stm32_dma_transfer(STM32DmaState *s, uint8_t idx, bool m2m)
{
    STM32DmaChannel *chan = &s->chan[idx];
    MemTxResult res = 0;

    if (!chan->enabled || chan->chcnt == 0) {
        trace_stm32_dma_disabled(idx, chan->enabled, chan->chcnt);
        return;
    }

    do {
        res = stm32_dma_transfer_one(s, idx);
        if (res) {
            goto fail_rw;
        }

        chan->chcnt--;
    } while (chan->chcnt && m2m);

    /* rearm counter */
    if (chan->chcnt == 0) {
        if (FIELD_EX32(chan->chctl, DMA_CCR, TCIE)) {
            /* Issue completed full transfer interrupt */
            trace_stm32_dma_raise(idx, "TCIE");
            set_bit(idx * 4 + 1, (unsigned long *)&s->intf);
        }

        /* M2M mode can't be circular */
        if (!FIELD_EX32(chan->chctl, DMA_CCR, M2M) &&
            FIELD_EX32(chan->chctl, DMA_CCR, CIRC)) {
            chan->chcnt = chan->chcnt_shadow;
            trace_stm32_dma_cmen(idx, chan->chcnt);
        }
    }

fail_rw:
    stm32_dma_chan_update_intr(s, idx);
    return;
}

static uint32_t stm32_dma_chan_read(STM32DmaState *s, hwaddr addr)
{
    uint8_t idx = (addr - A_DMA_CCR0) / 0x14 /* STRIDE */;
    uint8_t reg = (addr - 0x14 /* STRIDE */ * idx);
    uint32_t val = 0;

    if (idx > s->nr_chans) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: chan_idx %d exceed %d number of channels\n",
                      __func__, idx, s->nr_chans);
        return val;
    }

    switch (reg) {
    case A_DMA_CCR:
        val = s->chan[idx].chctl;
        break;
    case A_DMA_CNDTR:
        val = s->chan[idx].chcnt;
        break;
    case A_DMA_CPAR:
        val = s->chan[idx].chpaddr;
        break;
    case A_DMA_CMAR:
        val = s->chan[idx].chmaddr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unknown reg 0x%x\n",
                      __func__, reg);
        break;
    }

    trace_stm32_dma_chan_read(addr, idx, reg, val);

    return val;
}

static uint64_t stm32_dma_read(void *opaque, hwaddr addr, unsigned int size)
{
    STM32DmaState *s = STM32_DMA(opaque);
    uint32_t val = 0;

    switch (addr) {
    case A_DMA_ISR:
        val = s->intf;
        break;
    case A_DMA_CCR0 ... A_DMA_CMAR7:
        val = stm32_dma_chan_read(s, addr);
        break;
    /* write-only */
    case A_DMA_IFCR:
    default:
        /*
         * TODO: WARN_ONCE ? If left as is produces spam, because many
         *       people use '|=' on write-only registers.
         * qemu_log_mask(LOG_GUEST_ERROR,
         * "%s:  read to unimplemented register " \
         * "at address: 0x%" PRIx64 " size %d\n",
         * __func__, addr, size);
         */
        break;
    };

    trace_stm32_dma_read(addr);

    return val;
}

static void stm32_dma_update_chan_ctrl(STM32DmaState *s, uint8_t idx,
                                      uint32_t val)
{
    int is_enabled, was_enabled;

    was_enabled = !!FIELD_EX32(s->chan[idx].chctl, DMA_CCR, EN);
    is_enabled = !!FIELD_EX32(val, DMA_CCR, EN);

    if (was_enabled && is_enabled) {
        uint32_t protected = (s->chan[idx].chctl ^ val) & DMA_CCR_RO_MASK;
        if (protected) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: attempt to write enabled chan_idx %d settings "
                          "with val 0x%x\n", __func__, idx, val);
        }

        val &= ~DMA_CCR_RO_MASK;
        val |= DMA_CCR_RO_MASK & s->chan[idx].chctl;
    }

    s->chan[idx].chctl = val;
    s->chan[idx].enabled = !!FIELD_EX32(s->chan[idx].chctl, DMA_CCR, EN);

    if (!was_enabled && is_enabled) {
        if (FIELD_EX32(s->chan[idx].chctl, DMA_CCR, M2M)) {
            stm32_dma_transfer(s, idx, true);
        }
    }
}

static void stm32_dma_chan_write(STM32DmaState *s, hwaddr addr,
                                uint64_t val)
{
    uint8_t idx = (addr - A_DMA_CCR0) / 0x14 /* STRIDE */;
    uint8_t reg = (addr - 0x14 /* STRIDE */ * idx);

    if (idx > s->nr_chans) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: chan_idx %d exceed %d number of channels\n",
                      __func__, idx, s->nr_chans);
        return;
    }

    trace_stm32_dma_chan_write(addr, idx, reg, val);

    if (reg == A_DMA_CCR) {
        stm32_dma_update_chan_ctrl(s, idx, val);
        return;
    }

    if (s->chan[idx].enabled) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: attempt to changed enabled chan_idx %d settings\n",
                      __func__, idx);
        return;
    }

    switch (reg) {
    case A_DMA_CNDTR:
        s->chan[idx].chcnt = FIELD_EX32(val, DMA_CNDTR, NDT);
        s->chan[idx].chcnt_shadow = s->chan[idx].chcnt;
        break;
    case A_DMA_CPAR:
        s->chan[idx].chpaddr = val;
        break;
    case A_DMA_CMAR:
        s->chan[idx].chmaddr = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unknown reg 0x%x\n",
                      __func__, reg);
        break;
    }
}

static void stm32_dma_intr_ack(STM32DmaState *s, uint32_t val)
{
    int i, j;
    uint32_t changed = val & s->intf;

    if (!changed) {
        return;
    }

    for (i = 0, j = 0; i < R_DMA_IFCR_RSVD_SHIFT; i += 4, j++) {
        uint8_t bits_changed = extract32(changed, i, 4);
        if (bits_changed) {
            /* clear bits */
            uint8_t bits = 0;

            /* Clear global interrupt flag of channel */
            if (!(bits_changed & BIT(0))) {
                bits = extract32(s->intf, i, 4) & ~bits_changed;
            }

            s->intf = deposit32(s->intf, i , 4, bits);
            if (!bits) {
                trace_stm32_dma_lower(j);
                qemu_irq_lower(s->output[j]);
            }
        }
    }
}

static void stm32_dma_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned int size)
{
    STM32DmaState *s = STM32_DMA(opaque);

    switch (addr) {
    case A_DMA_IFCR:
        stm32_dma_intr_ack(s, val);
        break;
    case A_DMA_CCR0 ... A_DMA_CMAR7:
        stm32_dma_chan_write(s, addr, val);
        break;
    /* read-only */
    case A_DMA_ISR:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s:  write to unimplemented register " \
                      "at address: 0x%" PRIx64 " size=%d val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        break;
    };

    trace_stm32_dma_write(addr, val);
}

static const MemoryRegionOps dma_ops = {
    .read =  stm32_dma_read,
    .write = stm32_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void stm32_dma_reset_enter(Object *obj, ResetType type)
{
    STM32DmaState *s = STM32_DMA(obj);

    s->intf = 0x0;

    for (int i = 0; i < s->nr_chans; i++) {
        s->chan[i].chctl = 0;
        s->chan[i].chcnt = 0;
        s->chan[i].chpaddr = 0;
        s->chan[i].chmaddr = 0;

        s->chan[i].enabled = false;
    }

    trace_stm32_dma_reset("reset_enter");
}

static void stm32_dma_reset_hold(Object *obj, ResetType type)
{
    STM32DmaState *s = STM32_DMA(obj);

    for (int i = 0; i < s->nr_chans; i++) {
        qemu_irq_lower(s->output[i]);
    }

    trace_stm32_dma_reset("reset_hold");
}

/* irq from peripheral */
static void stm32_dma_set(void *opaque, int line, int value)
{
    STM32DmaState *s = STM32_DMA(opaque);

    if (line > s->nr_chans) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s:  requested non-existant line %d > %d\n",
                      __func__, line, s->nr_chans);
        return;
    }

    /* start dma transfer */
    stm32_dma_transfer(s, line, false);

    trace_stm32_dma_set(line, value);
}

static void stm32_dma_realize(DeviceState *dev, Error **errp)
{
    STM32DmaState *s = STM32_DMA(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &dma_ops, s,
            TYPE_STM32_DMA, STM32_DMA_APERTURE_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    qdev_init_gpio_in(DEVICE(s), stm32_dma_set, s->nr_chans);
    for (int i = 0; i < s->nr_chans; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->output[i]);
    }
}

static const VMStateDescription vmstate_stm32_dma_channel = {
    .name = "stm32_dma_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(enabled, STM32DmaChannel),
        VMSTATE_UINT32(chctl, STM32DmaChannel),
        VMSTATE_UINT32(chcnt, STM32DmaChannel),
        VMSTATE_UINT32(chpaddr, STM32DmaChannel),
        VMSTATE_UINT32(chmaddr, STM32DmaChannel),
        VMSTATE_UINT32(chcnt_shadow, STM32DmaChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_stm32_dma = {
    .name = TYPE_STM32_DMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(nr_chans, STM32DmaState),
        VMSTATE_UINT32(intf,    STM32DmaState),
        VMSTATE_STRUCT_ARRAY(chan, STM32DmaState, STM32_DMA_CHAN_NUMBER,
                             1, vmstate_stm32_dma_channel, STM32DmaChannel),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property stm32_dma_properties[] = {
    DEFINE_PROP_UINT8("nchans", STM32DmaState, nr_chans, STM32_DMA_CHAN_NUMBER),
};

static void stm32_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = stm32_dma_reset_enter;
    rc->phases.hold = stm32_dma_reset_hold;

    dc->vmsd = &vmstate_stm32_dma;
    dc->realize = stm32_dma_realize;
    dc->desc = "STM32 DMA";

    device_class_set_props(dc, stm32_dma_properties);
}

static const TypeInfo stm32_dma_info = {
    .name = TYPE_STM32_DMA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32DmaState),
    .class_init = stm32_dma_class_init,
};

static void stm32_dma_register_types(void)
{
    type_register_static(&stm32_dma_info);
}

type_init(stm32_dma_register_types)
