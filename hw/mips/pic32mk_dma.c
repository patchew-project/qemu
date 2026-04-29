/*
 * PIC32MK DMA Controller × 8 channels
 * Datasheet: DS60001519E, §26
 *
 * Phase 2B stub: accepts all register reads/writes, logs unimplemented
 * channel transfers.  Actual DMA transfer engine is deferred.
 *
 * Memory map (base physical 0x1F811000 = SFR_BASE + DMA_OFFSET):
 *   +0x000: DMACON (global control)
 *   +0x010: DMASTAT
 *   +0x020: DMAADDR
 *   +0x060: Channel 0 registers (DCH0CON, DCH0ECON, DCH0INT, ...)
 *   +0x120: Channel 1 registers
 *   ...  (stride 0xC0 per channel)
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/mips/pic32mk.h"

/* Total MMIO size: channel 7 ends at 0x060 + 7*0xC0 + 0xC0 = 0x060 + 0x780 = 0x7E0 */
#define DMA_MMIO_SIZE   0x1000u  /* full 4 KB page */

#define TYPE_PIC32MK_DMA    "pic32mk-dma"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKDMAState, PIC32MK_DMA)

/* Per-channel register state */
typedef struct {
    uint32_t con;   /* DCHxCON */
    uint32_t econ;  /* DCHxECON */
    uint32_t ireg;  /* DCHxINT */
    uint32_t ssa;   /* source start address */
    uint32_t dsa;   /* destination start address */
    uint32_t ssiz;  /* source size */
    uint32_t dsiz;  /* destination size */
    uint32_t sptr;  /* source pointer */
    uint32_t dptr;  /* destination pointer */
    uint32_t csiz;  /* cell size */
    uint32_t cptr;  /* cell pointer */
    uint32_t dat;   /* pattern data */
} DMAChannel;

struct PIC32MKDMAState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t   con;     /* DMACON */
    uint32_t   stat;    /* DMASTAT */
    uint32_t   addr;    /* DMAADDR */

    DMAChannel ch[PIC32MK_DMA_NCHANNELS];
    qemu_irq   irq[PIC32MK_DMA_NCHANNELS];
};

/*
 * Helpers
 * -----------------------------------------------------------------------
 */

/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

/* Map channel register offset to state field */
static uint32_t *dma_ch_reg(DMAChannel *ch, hwaddr off)
{
    switch (off) {
    case PIC32MK_DCHxCON:
        return &ch->con;
    case PIC32MK_DCHxECON:
        return &ch->econ;
    case PIC32MK_DCHxINT:
        return &ch->ireg;
    case PIC32MK_DCHxSSA:
        return &ch->ssa;
    case PIC32MK_DCHxDSA:
        return &ch->dsa;
    case PIC32MK_DCHxSSIZ:
        return &ch->ssiz;
    case PIC32MK_DCHxDSIZ:
        return &ch->dsiz;
    case PIC32MK_DCHxSPTR:
        return &ch->sptr;
    case PIC32MK_DCHxDPTR:
        return &ch->dptr;
    case PIC32MK_DCHxCSIZ:
        return &ch->csiz;
    case PIC32MK_DCHxCPTR:
        return &ch->cptr;
    case PIC32MK_DCHxDAT:
        return &ch->dat;
    default:
        return NULL;
    }
}

/*
 * MMIO
 * -----------------------------------------------------------------------
 */

static uint64_t dma_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKDMAState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;

    (void)sub;  /* reads always return base register value */

    /* Global registers */
    if (base == PIC32MK_DMACON_OFFSET) {
        return s->con;
    }
    if (base == PIC32MK_DMASTAT_OFFSET) {
        return s->stat;
    }
    if (base == PIC32MK_DMAADDR_OFFSET) {
        return s->addr;
    }

    /* Channel registers */
    if (addr >= PIC32MK_DMA_CH_BASE) {
        hwaddr ch_off = base - PIC32MK_DMA_CH_BASE;
        int ch_idx = (int)(ch_off / PIC32MK_DMA_CH_STRIDE);
        hwaddr reg_off = ch_off % PIC32MK_DMA_CH_STRIDE;

        if (ch_idx < PIC32MK_DMA_NCHANNELS) {
            uint32_t *reg = dma_ch_reg(&s->ch[ch_idx], reg_off);
            if (reg) {
                return *reg;
            }
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_dma: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void dma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKDMAState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;

    /* Global registers */
    if (base == PIC32MK_DMACON_OFFSET) {
        apply_sci(&s->con, (uint32_t)val, sub);
        return;
    }
    if (base == PIC32MK_DMASTAT_OFFSET || base == PIC32MK_DMAADDR_OFFSET) {
        return;  /* read-only */
    }

    /* Channel registers */
    if (addr >= PIC32MK_DMA_CH_BASE) {
        hwaddr ch_off  = base - PIC32MK_DMA_CH_BASE;
        int    ch_idx  = (int)(ch_off / PIC32MK_DMA_CH_STRIDE);
        hwaddr reg_off = ch_off % PIC32MK_DMA_CH_STRIDE;

        if (ch_idx < PIC32MK_DMA_NCHANNELS) {
            uint32_t *reg = dma_ch_reg(&s->ch[ch_idx], reg_off);
            if (reg) {
                apply_sci(reg, (uint32_t)val, sub);
                /* TODO: trigger DMA transfer if CHEN and FORCE bits set */
                return;
            }
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_dma: unimplemented write @ 0x%04"
                  HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                  addr, val);
}

static const MemoryRegionOps dma_ops = {
    .read       = dma_read,
    .write      = dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_dma_reset(DeviceState *dev)
{
    PIC32MKDMAState *s = PIC32MK_DMA(dev);
    s->con  = 0x80000000u;  /* DMABUSY=0, ON=1 on reset per datasheet */
    s->stat = 0;
    s->addr = 0;
    memset(s->ch, 0, sizeof(s->ch));
    for (int i = 0; i < PIC32MK_DMA_NCHANNELS; i++) {
        qemu_irq_lower(s->irq[i]);
    }
}

static void pic32mk_dma_init(Object *obj)
{
    PIC32MKDMAState *s = PIC32MK_DMA(obj);

    memory_region_init_io(&s->mr, obj, &dma_ops, s,
                          TYPE_PIC32MK_DMA, DMA_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    for (int i = 0; i < PIC32MK_DMA_NCHANNELS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
}

static void pic32mk_dma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_dma_reset);
}

static const TypeInfo pic32mk_dma_info = {
    .name          = TYPE_PIC32MK_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKDMAState),
    .instance_init = pic32mk_dma_init,
    .class_init    = pic32mk_dma_class_init,
};

static void pic32mk_dma_register_types(void)
{
    type_register_static(&pic32mk_dma_info);
}

type_init(pic32mk_dma_register_types)
