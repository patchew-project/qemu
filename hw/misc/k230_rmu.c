/*
 * K230 Reset Management Unit (RMU / SYSCTL_RST)
 *
 * The RMU is a bank of reset-control registers at 0x91101000. Each register
 * gathers the software-controllable reset lines of a group of peripherals.
 * Register semantics are modelled after the five reset types described in the
 * Linux mainline driver drivers/reset/reset-k230.c:
 *
 *   - CPU0/CPU1 : have a high-half write-enable strobe and a done bit (bit12).
 *   - FLUSH     : bit4 of the CPU registers; hardware auto-clears it, no done.
 *   - HW_DONE   : no write-enable; a done bit is latched when the reset fires
 *                 and cleared by software (write-1-to-clear).
 *   - SW_DONE   : plain read/write storage, no write-enable and no done bit.
 *
 * QEMU never actually resets the target peripherals, so the model collapses
 * the hardware reset latency to zero: writing a reset request bit latches the
 * matching done bit in the same access and auto-clears the request bit, which
 * lets the kernel's readl_poll_timeout() loops succeed on the first read.
 *
 * Copyright (c) 2026 Jack Wang <163wangjack@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/misc/k230_rmu.h"
#include "trace.h"

/* Sentinel meaning "this reset request bit has no associated done bit". */
#define K230_RMU_NO_DONE  0xff

/* A single (reset request bit -> done bit) pairing within one register. */
typedef struct {
    uint8_t reset_bit;   /* reset request bit index, 0..15               */
    uint8_t done_bit;    /* matching done bit index, or K230_RMU_NO_DONE  */
} K230RmuPair;

/* Description of one control register. */
typedef struct {
    hwaddr             offset;   /* word-aligned register offset               */
    bool               has_we;   /* high 16 bits are write-enable (CPU0/CPU1)  */
    const K230RmuPair *pairs;    /* reset/done pairings, NULL for SW_DONE banks */
    unsigned           n_pairs;  /* number of pairings (0 for SW_DONE banks)   */
} K230RmuReg;

/*
 * Per-register (reset bit -> done bit) tables, copied verbatim from the bit
 * positions in the Linux driver's k230_resets[] table.
 */
static const K230RmuPair pairs_cpu0[]    = { {0, 12}, {4, K230_RMU_NO_DONE} };
static const K230RmuPair pairs_cpu1[]    = { {0, 12}, {4, K230_RMU_NO_DONE} };
static const K230RmuPair pairs_ai[]      = { {0, 31} };
static const K230RmuPair pairs_vpu[]     = { {0, 31} };
static const K230RmuPair pairs_hisys[]   = { {0, 4}, {1, 5} };  /* done in low bits */
static const K230RmuPair pairs_sdio[]    = { {0, 28}, {1, 29}, {2, 30} };
static const K230RmuPair pairs_usb[]     = { {0, 28}, {1, 29}, {0, 30}, {1, 31} };
static const K230RmuPair pairs_spi[]     = { {0, 28}, {1, 29}, {2, 30} };
static const K230RmuPair pairs_sec[]     = { {0, 31} };
static const K230RmuPair pairs_dma[]     = { {0, 28}, {1, 29} };
static const K230RmuPair pairs_decomp[]  = { {0, 31} };
static const K230RmuPair pairs_sram[]    = { {0, 28}, {2, 30}, {3, 31} }; /* bit1 SW_DONE */
static const K230RmuPair pairs_nonai2d[] = { {0, 31} };
static const K230RmuPair pairs_mctl[]    = { {0, 31} };
static const K230RmuPair pairs_isp[]     = { {6, 29}, {5, 28} }; /* other low bits SW_DONE */
static const K230RmuPair pairs_dpu[]     = { {0, 31} };
static const K230RmuPair pairs_disp[]    = { {0, 31} };
static const K230RmuPair pairs_gpu[]     = { {0, 31} };
static const K230RmuPair pairs_audio[]   = { {0, 31} };

#define K230_RMU_REG(off, we, p)  { (off), (we), (p), ARRAY_SIZE(p) }
#define K230_RMU_SW(off)          { (off), false, NULL, 0 }  /* pure SW_DONE bank */

static const K230RmuReg k230_rmu_regs[] = {
    K230_RMU_REG(K230_RMU_CPU0_CTRL,    true,  pairs_cpu0),
    K230_RMU_REG(K230_RMU_CPU1_CTRL,    true,  pairs_cpu1),
    K230_RMU_REG(K230_RMU_AI_CTRL,      false, pairs_ai),
    K230_RMU_REG(K230_RMU_VPU_CTRL,     false, pairs_vpu),
    K230_RMU_SW (K230_RMU_PERI0_CTRL),
    K230_RMU_SW (K230_RMU_PERI1_CTRL),
    K230_RMU_REG(K230_RMU_HISYS_CTRL,   false, pairs_hisys),
    K230_RMU_REG(K230_RMU_SDIO_CTRL,    false, pairs_sdio),
    K230_RMU_REG(K230_RMU_USB_CTRL,     false, pairs_usb),
    K230_RMU_REG(K230_RMU_SPI_CTRL,     false, pairs_spi),
    K230_RMU_REG(K230_RMU_SEC_CTRL,     false, pairs_sec),
    K230_RMU_REG(K230_RMU_DMA_CTRL,     false, pairs_dma),
    K230_RMU_REG(K230_RMU_DECOMP_CTRL,  false, pairs_decomp),
    K230_RMU_REG(K230_RMU_SRAM_CTRL,    false, pairs_sram),
    K230_RMU_REG(K230_RMU_NONAI2D_CTRL, false, pairs_nonai2d),
    K230_RMU_REG(K230_RMU_MCTL_CTRL,    false, pairs_mctl),
    K230_RMU_REG(K230_RMU_ISP_CTRL,     false, pairs_isp),
    K230_RMU_REG(K230_RMU_DPU_CTRL,     false, pairs_dpu),
    K230_RMU_REG(K230_RMU_DISP_CTRL,    false, pairs_disp),
    K230_RMU_REG(K230_RMU_GPU_CTRL,     false, pairs_gpu),
    K230_RMU_REG(K230_RMU_AUDIO_CTRL,   false, pairs_audio),
    K230_RMU_SW (K230_RMU_SPI2AXI_CTRL),
};

/* Look up the register description for a word-aligned offset, or NULL. */
static const K230RmuReg *k230_rmu_lookup(hwaddr offset)
{
    for (size_t i = 0; i < ARRAY_SIZE(k230_rmu_regs); i++) {
        if (k230_rmu_regs[i].offset == offset) {
            return &k230_rmu_regs[i];
        }
    }
    return NULL;
}

static uint64_t k230_rmu_read(void *opaque, hwaddr offset, unsigned size)
{
    K230RmuState *s = K230_RMU(opaque);
    uint32_t value;

    if ((offset & 0x3) || offset >= K230_RMU_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    /* All hardware side effects happen on write, so a read just returns the
     * backing store: done bits already latched, request bits already cleared.
     */
    value = s->regs[offset / 4];
    trace_k230_rmu_read(offset, value);
    return value;
}

static void k230_rmu_write(void *opaque, hwaddr offset,
                           uint64_t val64, unsigned size)
{
    K230RmuState *s = K230_RMU(opaque);
    const K230RmuReg *r;
    uint32_t v = (uint32_t)val64;
    uint32_t old, new_val, we_gate, done_gate;
    uint32_t reset_mask = 0, done_mask = 0, sw_mask;

    if ((offset & 0x3) || offset >= K230_RMU_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    trace_k230_rmu_write(offset, v);
    old = s->regs[offset / 4];
    r = k230_rmu_lookup(offset);

    if (!r) {
        /* Inside the 4 KiB window but not modelled: accept the write and log
         * it so unexpected accesses are visible while debugging.
         */
        qemu_log_mask(LOG_UNIMP,
                      "%s: write to unmodelled offset 0x%" HWADDR_PRIx
                      " = 0x%08x\n", __func__, offset, v);
        s->regs[offset / 4] = v;
        return;
    }

    /* Derive the reset/done masks for this register from its pairing table. */
    for (unsigned i = 0; i < r->n_pairs; i++) {
        reset_mask |= BIT(r->pairs[i].reset_bit);
        if (r->pairs[i].done_bit != K230_RMU_NO_DONE) {
            done_mask |= BIT(r->pairs[i].done_bit);
        }
    }

    /*
     * we_gate holds the low-half bits this write is allowed to modify. With
     * write-enable (CPU0/CPU1) a low bit n is writable only when the strobe
     * bit n+16 is also set; otherwise every low bit is directly writable.
     */
    we_gate = r->has_we ? ((v >> K230_RMU_WE_SHIFT) & 0xffffu) : 0xffffu;

    new_val = old;

    /* (A) SW_DONE storage: low-half bits that are neither reset nor done bits
     *     are plain read/write storage.
     */
    sw_mask = 0xffffu & ~reset_mask & ~done_mask & we_gate;
    new_val = (new_val & ~sw_mask) | (v & sw_mask);

    /* (B) Done bits are write-1-to-clear. CPU-type done bits sit in the low
     *     half and their clear is gated by write-enable (the strobe is folded
     *     into we_gate); HW_DONE done bits sit in the high half, clear direct.
     */
    done_gate = r->has_we ? (done_mask & we_gate) : done_mask;
    new_val &= ~(v & done_gate);

    /* (C) A reset request latches its paired done bit immediately (zero
     *     latency) and the request bit auto-clears so it can fire again.
     *     FLUSH-type pairs (done_bit == NO_DONE) only auto-clear.
     */
    for (unsigned i = 0; i < r->n_pairs; i++) {
        uint32_t rbit = BIT(r->pairs[i].reset_bit);
        bool fire = (v & rbit) &&
                    (!r->has_we || (v & (rbit << K230_RMU_WE_SHIFT)));

        if (fire) {
            if (r->pairs[i].done_bit != K230_RMU_NO_DONE) {
                new_val |= BIT(r->pairs[i].done_bit);
            } else {
                trace_k230_rmu_flush(offset);
            }
            new_val &= ~rbit;
        }
    }

    s->regs[offset / 4] = new_val;
}

static const MemoryRegionOps k230_rmu_ops = {
    .read       = k230_rmu_read,
    .write      = k230_rmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned       = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void k230_rmu_reset(DeviceState *dev)
{
    K230RmuState *s = K230_RMU(dev);

    trace_k230_rmu_reset_device();
    memset(s->regs, 0, sizeof(s->regs));
}

static void k230_rmu_realize(DeviceState *dev, Error **errp)
{
    K230RmuState *s = K230_RMU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &k230_rmu_ops, s,
                          TYPE_K230_RMU, K230_RMU_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const VMStateDescription vmstate_k230_rmu = {
    .name               = "k230.rmu",
    .version_id         = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, K230RmuState, K230_RMU_NUM_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static void k230_rmu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = k230_rmu_realize;
    device_class_set_legacy_reset(dc, k230_rmu_reset);
    dc->vmsd = &vmstate_k230_rmu;
    dc->desc = "K230 Reset Management Unit";
}

static const TypeInfo k230_rmu_info = {
    .name          = TYPE_K230_RMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230RmuState),
    .class_init    = k230_rmu_class_init,
};

static void k230_rmu_register_type(void)
{
    type_register_static(&k230_rmu_info);
}
type_init(k230_rmu_register_type)
