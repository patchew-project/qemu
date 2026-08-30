/*
 * RP2040 DMA emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/dma/rp2040_dma.h"
#include "hw/misc/rp2040_nyi.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define DMA_CH_SIZE              0x40
#define DMA_CH_READ_ADDR         0x00
#define DMA_CH_WRITE_ADDR        0x04
#define DMA_CH_TRANS_COUNT       0x08
#define DMA_CH_CTRL_TRIG         0x0c
#define DMA_CH_AL1_CTRL          0x10
#define DMA_CH_AL1_READ_ADDR     0x14
#define DMA_CH_AL1_WRITE_ADDR    0x18
#define DMA_CH_AL1_TRANS_COUNT   0x1c
#define DMA_CH_AL2_CTRL          0x20
#define DMA_CH_AL2_TRANS_COUNT   0x24
#define DMA_CH_AL2_READ_ADDR     0x28
#define DMA_CH_AL2_WRITE_ADDR    0x2c
#define DMA_CH_AL3_CTRL          0x30
#define DMA_CH_AL3_WRITE_ADDR    0x34
#define DMA_CH_AL3_TRANS_COUNT   0x38
#define DMA_CH_AL3_READ_ADDR     0x3c

#define DMA_INTR                 0x400
#define DMA_INTE0                0x404
#define DMA_INTF0                0x408
#define DMA_INTS0                0x40c
#define DMA_INTE1                0x414
#define DMA_INTF1                0x418
#define DMA_INTS1                0x41c
#define DMA_TIMER0               0x420
#define DMA_MULTI_CHAN_TRIGGER   0x430
#define DMA_SNIFF_CTRL           0x434
#define DMA_SNIFF_DATA           0x438
#define DMA_FIFO_LEVELS          0x440
#define DMA_CHAN_ABORT           0x444

#define DMA_CTRL_AHB_ERROR       BIT(31)
#define DMA_CTRL_READ_ERROR      BIT(30)
#define DMA_CTRL_WRITE_ERROR     BIT(29)
#define DMA_CTRL_BUSY            BIT(24)
#define DMA_CTRL_SNIFF_EN        BIT(23)
#define DMA_CTRL_BSWAP           BIT(22)
#define DMA_CTRL_IRQ_QUIET       BIT(21)
#define DMA_CTRL_TREQ_SEL_SHIFT  15
#define DMA_CTRL_TREQ_SEL_MASK   (0x3f << DMA_CTRL_TREQ_SEL_SHIFT)
#define DMA_CTRL_CHAIN_TO_SHIFT  11
#define DMA_CTRL_CHAIN_TO_MASK   (0xf << DMA_CTRL_CHAIN_TO_SHIFT)
#define DMA_CTRL_RING_SEL        BIT(10)
#define DMA_CTRL_RING_SIZE_SHIFT 6
#define DMA_CTRL_RING_SIZE_MASK  (0xf << 6)
#define DMA_CTRL_INCR_WRITE      BIT(5)
#define DMA_CTRL_INCR_READ       BIT(4)
#define DMA_CTRL_DATA_SIZE_SHIFT 2
#define DMA_CTRL_DATA_SIZE_MASK  (0x3 << DMA_CTRL_DATA_SIZE_SHIFT)
#define DMA_CTRL_EN              BIT(0)
#define DMA_CTRL_ERROR_MASK      (DMA_CTRL_AHB_ERROR | \
                                  DMA_CTRL_READ_ERROR | \
                                  DMA_CTRL_WRITE_ERROR)

#define DMA_SNIFF_CTRL_OUT_INV   BIT(11)
#define DMA_SNIFF_CTRL_OUT_REV   BIT(10)
#define DMA_SNIFF_CTRL_BSWAP     BIT(9)
#define DMA_SNIFF_CTRL_CALC_SHIFT 5
#define DMA_SNIFF_CTRL_CALC_MASK  (0xf << DMA_SNIFF_CTRL_CALC_SHIFT)
#define DMA_SNIFF_CTRL_DMACH_SHIFT 1
#define DMA_SNIFF_CTRL_DMACH_MASK (0xf << DMA_SNIFF_CTRL_DMACH_SHIFT)
#define DMA_SNIFF_CTRL_EN        BIT(0)
#define DMA_SNIFF_CTRL_MASK      0xfff

#define DMA_SNIFF_CALC_CRC32     0x0
#define DMA_SNIFF_CALC_CRC32R    0x1
#define DMA_SNIFF_CALC_CRC16     0x2
#define DMA_SNIFF_CALC_CRC16R    0x3
#define DMA_SNIFF_CALC_EVEN      0xe
#define DMA_SNIFF_CALC_SUM       0xf

#define DMA_CTRL_WRITABLE_MASK   (DMA_CTRL_SNIFF_EN | DMA_CTRL_BSWAP | \
                                  DMA_CTRL_IRQ_QUIET | \
                                  DMA_CTRL_TREQ_SEL_MASK | \
                                  DMA_CTRL_CHAIN_TO_MASK | \
                                  DMA_CTRL_RING_SEL | \
                                  DMA_CTRL_RING_SIZE_MASK | \
                                  DMA_CTRL_INCR_WRITE | \
                                  DMA_CTRL_INCR_READ | \
                                  DMA_CTRL_DATA_SIZE_MASK | \
                                  BIT(1) | DMA_CTRL_EN)
#define DMA_CHANNEL_MASK         ((1u << RP2040_DMA_NUM_CHANNELS) - 1)
#define DMA_PACING_SYSCLK_HZ     125000000ULL
#define DMA_PACING_SYSCLK_NS     8ULL

#define ATOMIC_ALIAS_MASK        0x3000
#define ATOMIC_XOR               0x1000
#define ATOMIC_SET               0x2000
#define ATOMIC_CLR               0x3000

static uint32_t rp2040_dma_apply_alias(uint32_t old, uint32_t value,
                                       hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static uint32_t rp2040_dma_ints(RP2040DmaState *s, unsigned irq)
{
    return ((s->intr & s->inte[irq]) | s->intf[irq]) & DMA_CHANNEL_MASK;
}

static void rp2040_dma_update_irq(RP2040DmaState *s)
{
    int i;

    for (i = 0; i < RP2040_DMA_NUM_IRQS; i++) {
        qemu_set_irq(s->irq[i], rp2040_dma_ints(s, i) != 0);
    }
}

static unsigned rp2040_dma_transfer_size(RP2040DmaChannel *ch)
{
    switch ((ch->ctrl & DMA_CTRL_DATA_SIZE_MASK) >> DMA_CTRL_DATA_SIZE_SHIFT) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 4;
    default:
        return 4;
    }
}

static void rp2040_dma_request_start(RP2040DmaState *s, unsigned index);
static void rp2040_dma_dreq(void *opaque, int n, int level);
static void rp2040_dma_dreq_pulse(RP2040DmaState *s, uint32_t dreq);
static void rp2040_dma_dreq_bh(void *opaque);
static bool rp2040_dma_dreq_has_busy_channel(RP2040DmaState *s, uint32_t dreq,
                                             unsigned except);
static void rp2040_dma_write_reg(RP2040DmaState *s, hwaddr addr,
                                 uint64_t value64, unsigned size);

static uint32_t rp2040_dma_treq(RP2040DmaChannel *ch)
{
    return (ch->ctrl & DMA_CTRL_TREQ_SEL_MASK) >> DMA_CTRL_TREQ_SEL_SHIFT;
}

static bool rp2040_dma_treq_is_ready_sink(uint32_t treq)
{
    /*
     * The current XIP/SSI TX model does not have a finite TX FIFO: writes to
     * its data register are accepted immediately.
     */
    return treq == RP2040_DREQ_XIP_SSITX;
}

static bool rp2040_dma_treq_is_connected_level(uint32_t treq)
{
    return treq == RP2040_DREQ_UART0_TX || treq == RP2040_DREQ_UART0_RX ||
           treq == RP2040_DREQ_UART1_TX || treq == RP2040_DREQ_UART1_RX ||
           treq == RP2040_DREQ_XIP_STREAM;
}

static bool rp2040_dma_treq_is_timer(uint32_t treq)
{
    return treq >= RP2040_DREQ_DMA_TIMER0 &&
           treq <= RP2040_DREQ_DMA_TIMER3;
}

static uint64_t rp2040_dma_timer_period_ns(uint32_t value)
{
    uint32_t x = value >> 16;
    uint32_t y = value & 0xffff;
    uint64_t period;

    if (x == 0 || y == 0) {
        return 0;
    }

    /*
     * The RP2040 fractional timer emits TREQs at (X/Y) * sys_clk, capped
     * at one TREQ per sys_clk. Use the Pico's nominal 125 MHz system clock
     * as the virtual pacing source.
     */
    if (x >= y) {
        return DMA_PACING_SYSCLK_NS;
    }

    period = DIV_ROUND_UP((uint64_t)y * NANOSECONDS_PER_SECOND,
                          (uint64_t)x * DMA_PACING_SYSCLK_HZ);
    return MAX(period, 1);
}

static void rp2040_dma_timer_update(RP2040DmaState *s, unsigned index)
{
    RP2040DmaPacingTimer *pt = &s->pacing_timer[index];

    pt->period_ns = rp2040_dma_timer_period_ns(s->timer[index]);
    if (pt->period_ns == 0) {
        timer_del(pt->timer);
        return;
    }

    timer_mod(pt->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              pt->period_ns);
}

static void rp2040_dma_timer_cb(void *opaque)
{
    RP2040DmaPacingTimer *pt = opaque;
    RP2040DmaState *s = pt->dma;
    uint32_t dreq = RP2040_DREQ_DMA_TIMER0 + pt->index;

    if (rp2040_dma_dreq_has_busy_channel(s, dreq, RP2040_DMA_NUM_CHANNELS)) {
        rp2040_dma_dreq_pulse(s, dreq);
    }
    if (pt->period_ns != 0) {
        timer_mod(pt->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  pt->period_ns);
    }
}

static uint32_t rp2040_dma_next_addr(RP2040DmaChannel *ch, uint32_t addr,
                                     unsigned width, bool write)
{
    uint32_t ring_size;
    bool ring_write;
    uint32_t mask;

    if (!(ch->ctrl & (write ? DMA_CTRL_INCR_WRITE : DMA_CTRL_INCR_READ))) {
        return addr;
    }

    ring_size = (ch->ctrl & DMA_CTRL_RING_SIZE_MASK) >>
                DMA_CTRL_RING_SIZE_SHIFT;
    ring_write = ch->ctrl & DMA_CTRL_RING_SEL;
    if (ring_size == 0 || ring_write != write) {
        return addr + width;
    }

    mask = (1u << ring_size) - 1;
    return (addr & ~mask) | ((addr + width) & mask);
}

static uint32_t rp2040_dma_sniff_word(const uint8_t *buf, unsigned width)
{
    uint32_t value = buf[0];

    if (width >= 2) {
        value |= (uint32_t)buf[1] << 8;
    }
    if (width >= 4) {
        value |= (uint32_t)buf[2] << 16;
        value |= (uint32_t)buf[3] << 24;
    }
    return value;
}

static void rp2040_dma_sniff_bswap(uint8_t *buf, unsigned width)
{
    if (width == 2) {
        uint8_t t = buf[0];
        buf[0] = buf[1];
        buf[1] = t;
    } else if (width == 4) {
        uint8_t t = buf[0];
        buf[0] = buf[3];
        buf[3] = t;
        t = buf[1];
        buf[1] = buf[2];
        buf[2] = t;
    }
}

static uint32_t rp2040_dma_sniff_crc32(uint32_t crc, const uint8_t *buf,
                                       unsigned width, bool reverse)
{
    int i;
    int bit;

    for (i = 0; i < width; i++) {
        uint8_t byte = reverse ? revbit8(buf[i]) : buf[i];

        crc ^= (uint32_t)byte << 24;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & BIT(31)) ? (crc << 1) ^ 0x04c11db7 : crc << 1;
        }
    }
    return crc;
}

static uint32_t rp2040_dma_sniff_crc16(uint32_t crc, const uint8_t *buf,
                                       unsigned width, bool reverse)
{
    uint16_t crc16 = crc;
    int i;
    int bit;

    for (i = 0; i < width; i++) {
        uint8_t byte = reverse ? revbit8(buf[i]) : buf[i];

        crc16 ^= (uint16_t)byte << 8;
        for (bit = 0; bit < 8; bit++) {
            crc16 = (crc16 & BIT(15)) ? (crc16 << 1) ^ 0x1021 : crc16 << 1;
        }
    }
    return (crc & 0xffff0000) | crc16;
}

static void rp2040_dma_sniff_update(RP2040DmaState *s, unsigned index,
                                    const uint8_t *buf, unsigned width)
{
    RP2040DmaChannel *ch = &s->chan[index];
    uint8_t sniff_buf[4] = { 0 };
    uint32_t channel;
    uint32_t calc;
    uint32_t value;

    if (!(s->sniff_ctrl & DMA_SNIFF_CTRL_EN) ||
        !(ch->ctrl & DMA_CTRL_SNIFF_EN)) {
        return;
    }

    channel = (s->sniff_ctrl & DMA_SNIFF_CTRL_DMACH_MASK) >>
              DMA_SNIFF_CTRL_DMACH_SHIFT;
    if (channel != index) {
        return;
    }

    memcpy(sniff_buf, buf, width);
    if (s->sniff_ctrl & DMA_SNIFF_CTRL_BSWAP) {
        rp2040_dma_sniff_bswap(sniff_buf, width);
    }

    calc = (s->sniff_ctrl & DMA_SNIFF_CTRL_CALC_MASK) >>
           DMA_SNIFF_CTRL_CALC_SHIFT;
    switch (calc) {
    case DMA_SNIFF_CALC_CRC32:
        s->sniff_data = rp2040_dma_sniff_crc32(s->sniff_data, sniff_buf,
                                               width, false);
        break;
    case DMA_SNIFF_CALC_CRC32R:
        s->sniff_data = rp2040_dma_sniff_crc32(s->sniff_data, sniff_buf,
                                               width, true);
        break;
    case DMA_SNIFF_CALC_CRC16:
        s->sniff_data = rp2040_dma_sniff_crc16(s->sniff_data, sniff_buf,
                                               width, false);
        break;
    case DMA_SNIFF_CALC_CRC16R:
        s->sniff_data = rp2040_dma_sniff_crc16(s->sniff_data, sniff_buf,
                                               width, true);
        break;
    case DMA_SNIFF_CALC_EVEN:
        value = rp2040_dma_sniff_word(sniff_buf, width);
        s->sniff_data = (s->sniff_data ^ ctpop32(value)) & 1;
        break;
    case DMA_SNIFF_CALC_SUM:
        s->sniff_data += rp2040_dma_sniff_word(sniff_buf, width);
        break;
    default:
        break;
    }
}

static uint32_t rp2040_dma_sniff_read_data(RP2040DmaState *s)
{
    uint32_t value = s->sniff_data;

    if (s->sniff_ctrl & DMA_SNIFF_CTRL_OUT_REV) {
        value = revbit32(value);
    }
    if (s->sniff_ctrl & DMA_SNIFF_CTRL_OUT_INV) {
        value = ~value;
    }
    return value;
}

static void rp2040_dma_finish_channel(RP2040DmaState *s, unsigned index)
{
    RP2040DmaChannel *ch = &s->chan[index];
    uint32_t chain_to;

    ch->ctrl &= ~DMA_CTRL_BUSY;
    if (!(ch->ctrl & DMA_CTRL_IRQ_QUIET)) {
        s->intr |= BIT(index);
        rp2040_dma_update_irq(s);
    }

    chain_to = (ch->ctrl & DMA_CTRL_CHAIN_TO_MASK) >> DMA_CTRL_CHAIN_TO_SHIFT;
    if (!(ch->ctrl & DMA_CTRL_ERROR_MASK) &&
        chain_to < RP2040_DMA_NUM_CHANNELS && chain_to != index) {
        rp2040_dma_request_start(s, chain_to);
    }
}

static void rp2040_dma_run_beats(RP2040DmaState *s, unsigned index,
                                 uint32_t beats)
{
    RP2040DmaChannel *ch = &s->chan[index];
    uint32_t count;
    unsigned width;

    if (!(ch->ctrl & DMA_CTRL_EN) || ch->trans_count == 0) {
        return;
    }

    width = rp2040_dma_transfer_size(ch);
    count = MIN(ch->trans_count, beats);

    while (count--) {
        uint8_t buf[4] = { 0 };
        MemTxResult result;

        result = address_space_rw(&s->dma_as, ch->read_addr,
                                  MEMTXATTRS_UNSPECIFIED, buf, width, false);
        if (result != MEMTX_OK) {
            ch->ctrl |= DMA_CTRL_READ_ERROR | DMA_CTRL_AHB_ERROR;
            break;
        }

        if (ch->ctrl & DMA_CTRL_BSWAP) {
            if (width == 2) {
                uint8_t t = buf[0];
                buf[0] = buf[1];
                buf[1] = t;
            } else if (width == 4) {
                uint8_t t = buf[0];
                buf[0] = buf[3];
                buf[3] = t;
                t = buf[1];
                buf[1] = buf[2];
                buf[2] = t;
            }
        }
        rp2040_dma_sniff_update(s, index, buf, width);

        if (ch->write_addr >= RP2040_DMA_BASE &&
            ch->write_addr < RP2040_DMA_BASE + RP2040_DMA_SIZE) {
            if (width == 4) {
                rp2040_dma_write_reg(s, ch->write_addr - RP2040_DMA_BASE,
                                     ldl_le_p(buf), width);
                result = MEMTX_OK;
            } else {
                result = MEMTX_ERROR;
            }
        } else {
            result = address_space_rw(&s->dma_as, ch->write_addr,
                                      MEMTXATTRS_UNSPECIFIED, buf, width,
                                      true);
        }
        if (result != MEMTX_OK) {
            ch->ctrl |= DMA_CTRL_WRITE_ERROR | DMA_CTRL_AHB_ERROR;
            break;
        }

        ch->trans_count--;
        ch->read_addr = rp2040_dma_next_addr(ch, ch->read_addr, width, false);
        ch->write_addr = rp2040_dma_next_addr(ch, ch->write_addr, width, true);
    }

    if ((ch->ctrl & DMA_CTRL_ERROR_MASK) || ch->trans_count == 0) {
        rp2040_dma_finish_channel(s, index);
    }
}

static void rp2040_dma_start_channel(RP2040DmaState *s, unsigned index)
{
    RP2040DmaChannel *ch = &s->chan[index];
    uint32_t treq;

    if (!(ch->ctrl & DMA_CTRL_EN)) {
        return;
    }

    if (ch->trans_count == 0 && ch->reload_count != 0) {
        ch->trans_count = ch->reload_count;
    }

    if (ch->trans_count == 0) {
        if (ch->ctrl & DMA_CTRL_IRQ_QUIET) {
            s->intr |= BIT(index);
            rp2040_dma_update_irq(s);
        }
        return;
    }

    ch->ctrl |= DMA_CTRL_BUSY;
    ch->ctrl &= ~DMA_CTRL_ERROR_MASK;
    ch->paced_nyi_logged = false;

    treq = rp2040_dma_treq(ch);
    if (treq == RP2040_DREQ_FORCE || rp2040_dma_treq_is_ready_sink(treq)) {
        rp2040_dma_run_beats(s, index, UINT32_MAX);
    } else if ((rp2040_dma_treq_is_connected_level(treq) ||
                treq == RP2040_DREQ_XIP_SSIRX) && s->dreq_level[treq]) {
        rp2040_dma_dreq_pulse(s, treq);
    } else if (treq != RP2040_DREQ_XIP_SSIRX &&
               !rp2040_dma_treq_is_connected_level(treq) &&
               !rp2040_dma_treq_is_timer(treq) &&
               !ch->paced_nyi_logged) {
        rp2040_log_nyi("dma", "paced transfer",
                       "DREQ source is not connected yet");
        ch->paced_nyi_logged = true;
    }
}

static void rp2040_dma_drain_starts(RP2040DmaState *s)
{
    if (s->engine_active) {
        return;
    }

    s->engine_active = true;
    while (s->pending_start) {
        unsigned index = ctz32(s->pending_start);

        s->pending_start &= ~BIT(index);
        rp2040_dma_start_channel(s, index);
    }
    s->engine_active = false;
}

static void rp2040_dma_request_start(RP2040DmaState *s, unsigned index)
{
    if (index >= RP2040_DMA_NUM_CHANNELS) {
        return;
    }

    s->pending_start |= BIT(index);
    rp2040_dma_drain_starts(s);
}

static void rp2040_dma_dreq_pulse(RP2040DmaState *s, uint32_t dreq)
{
    if (dreq >= RP2040_DMA_NUM_DREQS) {
        return;
    }

    if (s->pending_dreq[dreq] != UINT32_MAX) {
        s->pending_dreq[dreq]++;
    }
    qemu_bh_schedule(s->dreq_bh);
}

static void rp2040_dma_dreq(void *opaque, int n, int level)
{
    RP2040DmaState *s = opaque;

    if (n < 0 || n >= RP2040_DMA_NUM_DREQS) {
        return;
    }

    s->dreq_level[n] = level;
    if (level) {
        rp2040_dma_dreq_pulse(s, n);
        if (rp2040_dma_treq_is_connected_level(n)) {
            rp2040_dma_dreq_bh(s);
        }
    }
}

static void rp2040_dma_dreq_bh(void *opaque)
{
    RP2040DmaState *s = opaque;
    int i;
    int dreq;

    if (s->dreq_servicing) {
        return;
    }

    s->dreq_servicing = true;
    for (dreq = 0; dreq < RP2040_DMA_NUM_DREQS; dreq++) {
        while (s->pending_dreq[dreq] > 0) {
            s->pending_dreq[dreq]--;
            for (i = 0; i < RP2040_DMA_NUM_CHANNELS; i++) {
                RP2040DmaChannel *ch = &s->chan[i];

                if ((ch->ctrl & DMA_CTRL_BUSY) && rp2040_dma_treq(ch) == dreq) {
                    s->engine_active = true;
                    rp2040_dma_run_beats(s, i, 1);
                    s->engine_active = false;
                    rp2040_dma_drain_starts(s);
                }
            }
            if (s->dreq_level[dreq] &&
                rp2040_dma_dreq_has_busy_channel(s, dreq,
                                                 RP2040_DMA_NUM_CHANNELS)) {
                rp2040_dma_dreq_pulse(s, dreq);
            }
        }
    }
    s->dreq_servicing = false;
}

static bool rp2040_dma_dreq_has_busy_channel(RP2040DmaState *s, uint32_t dreq,
                                             unsigned except)
{
    int i;

    for (i = 0; i < RP2040_DMA_NUM_CHANNELS; i++) {
        RP2040DmaChannel *ch = &s->chan[i];

        if (i != except && (ch->ctrl & DMA_CTRL_BUSY) &&
            rp2040_dma_treq(ch) == dreq) {
            return true;
        }
    }
    return false;
}

static void rp2040_dma_abort_channel(RP2040DmaState *s, unsigned index)
{
    RP2040DmaChannel *ch = &s->chan[index];
    uint32_t treq = rp2040_dma_treq(ch);

    /*
     * RP2040 CHAN_ABORT clears the transfer counter and leaves the channel
     * inactive. QEMU does not model in-flight bus-transfer latency, so the
     * abort status bit is clear as soon as the write completes.
     */
    ch->ctrl &= ~DMA_CTRL_BUSY;
    ch->trans_count = 0;
    if (treq < RP2040_DMA_NUM_DREQS &&
        !rp2040_dma_dreq_has_busy_channel(s, treq, index)) {
        s->pending_dreq[treq] = 0;
    }
}

static uint32_t rp2040_dma_read_channel(RP2040DmaState *s, unsigned index,
                                        hwaddr offset)
{
    RP2040DmaChannel *ch = &s->chan[index];

    switch (offset) {
    case DMA_CH_READ_ADDR:
    case DMA_CH_AL1_READ_ADDR:
    case DMA_CH_AL2_READ_ADDR:
    case DMA_CH_AL3_READ_ADDR:
        return ch->read_addr;
    case DMA_CH_WRITE_ADDR:
    case DMA_CH_AL1_WRITE_ADDR:
    case DMA_CH_AL2_WRITE_ADDR:
    case DMA_CH_AL3_WRITE_ADDR:
        return ch->write_addr;
    case DMA_CH_TRANS_COUNT:
    case DMA_CH_AL1_TRANS_COUNT:
    case DMA_CH_AL2_TRANS_COUNT:
    case DMA_CH_AL3_TRANS_COUNT:
        return ch->trans_count;
    case DMA_CH_CTRL_TRIG:
    case DMA_CH_AL1_CTRL:
    case DMA_CH_AL2_CTRL:
    case DMA_CH_AL3_CTRL:
        return ch->ctrl;
    default:
        return 0;
    }
}

static void rp2040_dma_write_ctrl(RP2040DmaState *s, unsigned index,
                                  uint32_t value, bool trigger)
{
    RP2040DmaChannel *ch = &s->chan[index];
    uint32_t errors = ch->ctrl & DMA_CTRL_ERROR_MASK;

    errors &= ~(value & (DMA_CTRL_READ_ERROR | DMA_CTRL_WRITE_ERROR));
    if (errors & (DMA_CTRL_READ_ERROR | DMA_CTRL_WRITE_ERROR)) {
        errors |= DMA_CTRL_AHB_ERROR;
    } else {
        errors &= ~DMA_CTRL_AHB_ERROR;
    }
    ch->ctrl = (value & DMA_CTRL_WRITABLE_MASK) | errors;
    if (trigger) {
        rp2040_dma_request_start(s, index);
    }
}

static void rp2040_dma_write_channel(RP2040DmaState *s, unsigned index,
                                     hwaddr offset, uint32_t value)
{
    RP2040DmaChannel *ch = &s->chan[index];

    switch (offset) {
    case DMA_CH_READ_ADDR:
    case DMA_CH_AL1_READ_ADDR:
    case DMA_CH_AL2_READ_ADDR:
        ch->read_addr = value;
        break;
    case DMA_CH_AL3_READ_ADDR:
        ch->read_addr = value;
        rp2040_dma_request_start(s, index);
        break;
    case DMA_CH_WRITE_ADDR:
    case DMA_CH_AL1_WRITE_ADDR:
    case DMA_CH_AL3_WRITE_ADDR:
        ch->write_addr = value;
        break;
    case DMA_CH_AL2_WRITE_ADDR:
        ch->write_addr = value;
        rp2040_dma_request_start(s, index);
        break;
    case DMA_CH_TRANS_COUNT:
    case DMA_CH_AL2_TRANS_COUNT:
    case DMA_CH_AL3_TRANS_COUNT:
        ch->trans_count = value;
        ch->reload_count = value;
        break;
    case DMA_CH_AL1_TRANS_COUNT:
        ch->trans_count = value;
        ch->reload_count = value;
        rp2040_dma_request_start(s, index);
        break;
    case DMA_CH_CTRL_TRIG:
        rp2040_dma_write_ctrl(s, index, value, true);
        break;
    case DMA_CH_AL1_CTRL:
    case DMA_CH_AL2_CTRL:
    case DMA_CH_AL3_CTRL:
        rp2040_dma_write_ctrl(s, index, value, false);
        break;
    default:
        break;
    }
}

static uint64_t rp2040_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040DmaState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint32_t value;

    if (offset < RP2040_DMA_NUM_CHANNELS * DMA_CH_SIZE) {
        value = rp2040_dma_read_channel(s, offset / DMA_CH_SIZE,
                                        offset % DMA_CH_SIZE);
    } else {
        switch (offset) {
        case DMA_INTR:
            value = s->intr;
            break;
        case DMA_INTE0:
            value = s->inte[0];
            break;
        case DMA_INTF0:
            value = s->intf[0];
            break;
        case DMA_INTS0:
            value = rp2040_dma_ints(s, 0);
            break;
        case DMA_INTE1:
            value = s->inte[1];
            break;
        case DMA_INTF1:
            value = s->intf[1];
            break;
        case DMA_INTS1:
            value = rp2040_dma_ints(s, 1);
            break;
        case DMA_TIMER0 ... DMA_TIMER0 + 3 * sizeof(uint32_t):
            value = s->timer[(offset - DMA_TIMER0) / sizeof(uint32_t)];
            break;
        case DMA_SNIFF_CTRL:
            value = s->sniff_ctrl;
            break;
        case DMA_SNIFF_DATA:
            value = rp2040_dma_sniff_read_data(s);
            break;
        case DMA_FIFO_LEVELS:
            value = 0;
            break;
        case DMA_CHAN_ABORT:
            value = 0;
            break;
        default:
            value = 0;
            qemu_log_mask(LOG_UNIMP, "rp2040.dma: unimplemented read  "
                          "(size %d, addr 0x%08" HWADDR_PRIx
                          ", offset 0x%04" HWADDR_PRIx
                          ") -> 0x%0*" PRIx32 "\n",
                          size, RP2040_DMA_BASE + addr, offset,
                          size << 1, value);
            break;
        }
    }

    return value;
}

static void rp2040_dma_write_reg(RP2040DmaState *s, hwaddr addr,
                                 uint64_t value64, unsigned size)
{
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;
    uint32_t old;
    int i;

    if (offset < RP2040_DMA_NUM_CHANNELS * DMA_CH_SIZE) {
        rp2040_dma_write_channel(s, offset / DMA_CH_SIZE,
                                 offset % DMA_CH_SIZE, value);
    } else {
        switch (offset) {
        case DMA_INTR:
            s->intr &= ~(value & DMA_CHANNEL_MASK);
            rp2040_dma_update_irq(s);
            break;
        case DMA_INTE0:
            s->inte[0] = rp2040_dma_apply_alias(s->inte[0], value, alias) &
                         DMA_CHANNEL_MASK;
            rp2040_dma_update_irq(s);
            break;
        case DMA_INTF0:
            s->intf[0] = rp2040_dma_apply_alias(s->intf[0], value, alias) &
                         DMA_CHANNEL_MASK;
            rp2040_dma_update_irq(s);
            break;
        case DMA_INTS0:
            s->intr &= ~(value & DMA_CHANNEL_MASK);
            rp2040_dma_update_irq(s);
            break;
        case DMA_INTE1:
            s->inte[1] = rp2040_dma_apply_alias(s->inte[1], value, alias) &
                         DMA_CHANNEL_MASK;
            rp2040_dma_update_irq(s);
            break;
        case DMA_INTF1:
            s->intf[1] = rp2040_dma_apply_alias(s->intf[1], value, alias) &
                         DMA_CHANNEL_MASK;
            rp2040_dma_update_irq(s);
            break;
        case DMA_INTS1:
            s->intr &= ~(value & DMA_CHANNEL_MASK);
            rp2040_dma_update_irq(s);
            break;
        case DMA_TIMER0 ... DMA_TIMER0 + 3 * sizeof(uint32_t):
            i = (offset - DMA_TIMER0) / sizeof(uint32_t);
            s->timer[i] = rp2040_dma_apply_alias(s->timer[i], value, alias);
            rp2040_dma_timer_update(s, i);
            break;
        case DMA_MULTI_CHAN_TRIGGER:
            value &= DMA_CHANNEL_MASK;
            for (i = 0; i < RP2040_DMA_NUM_CHANNELS; i++) {
                if (value & BIT(i)) {
                    rp2040_dma_request_start(s, i);
                }
            }
            break;
        case DMA_SNIFF_CTRL:
            old = s->sniff_ctrl;
            s->sniff_ctrl = rp2040_dma_apply_alias(old, value, alias) &
                            DMA_SNIFF_CTRL_MASK;
            break;
        case DMA_SNIFF_DATA:
            s->sniff_data = rp2040_dma_apply_alias(s->sniff_data, value,
                                                   alias);
            break;
        case DMA_CHAN_ABORT:
            value &= DMA_CHANNEL_MASK;
            for (i = 0; i < RP2040_DMA_NUM_CHANNELS; i++) {
                if (value & BIT(i)) {
                    rp2040_dma_abort_channel(s, i);
                }
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "rp2040.dma: unimplemented write "
                          "(size %d, addr 0x%08" HWADDR_PRIx
                          ", offset 0x%04" HWADDR_PRIx
                          ", value 0x%0*" PRIx64 ")\n",
                          size, RP2040_DMA_BASE + addr, offset, size << 1,
                          value64);
            break;
        }
    }
}

static void rp2040_dma_write(void *opaque, hwaddr addr, uint64_t value64,
                             unsigned size)
{
    rp2040_dma_write_reg(opaque, addr, value64, size);
}

static const MemoryRegionOps rp2040_dma_ops = {
    .read = rp2040_dma_read,
    .write = rp2040_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_dma_reset(DeviceState *dev)
{
    RP2040DmaState *s = RP2040_DMA(dev);
    int i;

    for (i = 0; i < RP2040_DMA_NUM_CHANNELS; i++) {
        s->chan[i].read_addr = 0;
        s->chan[i].write_addr = 0;
        s->chan[i].trans_count = 0;
        s->chan[i].reload_count = 0;
        s->chan[i].ctrl = i << DMA_CTRL_CHAIN_TO_SHIFT;
        s->chan[i].paced_nyi_logged = false;
    }
    s->intr = 0;
    s->inte[0] = 0;
    s->inte[1] = 0;
    s->intf[0] = 0;
    s->intf[1] = 0;
    memset(s->timer, 0, sizeof(s->timer));
    for (i = 0; i < RP2040_DMA_NUM_TIMERS; i++) {
        s->pacing_timer[i].period_ns = 0;
        timer_del(s->pacing_timer[i].timer);
    }
    memset(s->dreq_level, 0, sizeof(s->dreq_level));
    memset(s->pending_dreq, 0, sizeof(s->pending_dreq));
    qemu_bh_cancel(s->dreq_bh);
    s->engine_active = false;
    s->pending_start = 0;
    s->sniff_ctrl = 0;
    s->sniff_data = 0;
    rp2040_dma_update_irq(s);
}

static void rp2040_dma_init(Object *obj)
{
    RP2040DmaState *s = RP2040_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&s->iomem, obj, &rp2040_dma_ops, s,
                          TYPE_RP2040_DMA, RP2040_DMA_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    for (i = 0; i < RP2040_DMA_NUM_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    s->dreq_bh = qemu_bh_new(rp2040_dma_dreq_bh, s);
    qdev_init_gpio_in_named(DEVICE(obj), rp2040_dma_dreq, "dreq",
                            RP2040_DMA_NUM_DREQS);
    for (i = 0; i < RP2040_DMA_NUM_TIMERS; i++) {
        s->pacing_timer[i].dma = s;
        s->pacing_timer[i].index = i;
        s->pacing_timer[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                                rp2040_dma_timer_cb,
                                                &s->pacing_timer[i]);
    }
}

static void rp2040_dma_finalize(Object *obj)
{
    RP2040DmaState *s = RP2040_DMA(obj);
    int i;

    for (i = 0; i < RP2040_DMA_NUM_TIMERS; i++) {
        timer_free(s->pacing_timer[i].timer);
    }
    qemu_bh_delete(s->dreq_bh);
}

static void rp2040_dma_realize(DeviceState *dev, Error **errp)
{
    RP2040DmaState *s = RP2040_DMA(dev);

    if (!s->dma_mr) {
        error_setg(errp, "memory property was not set");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, "rp2040-dma-memory");
}

static const VMStateDescription rp2040_dma_vmstate = {
    .name = TYPE_RP2040_DMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static const Property rp2040_dma_properties[] = {
    DEFINE_PROP_LINK("memory", RP2040DmaState, dma_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void rp2040_dma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp2040_dma_realize;
    device_class_set_legacy_reset(dc, rp2040_dma_reset);
    dc->vmsd = &rp2040_dma_vmstate;
    device_class_set_props(dc, rp2040_dma_properties);
}

static const TypeInfo rp2040_dma_info = {
    .name          = TYPE_RP2040_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040DmaState),
    .instance_init = rp2040_dma_init,
    .instance_finalize = rp2040_dma_finalize,
    .class_init    = rp2040_dma_class_init,
};

static void rp2040_dma_register_types(void)
{
    type_register_static(&rp2040_dma_info);
}
type_init(rp2040_dma_register_types)
