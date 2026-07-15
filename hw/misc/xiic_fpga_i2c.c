/*
 * xiic_fpga_i2c.c - QEMU model of a Nexthop-style PCIe FPGA that exposes
 *                   Xilinx AXI-IIC controllers, for emulating the SONiC
 *                   multifpgapci / i2c-xiic hardware path WITHOUT real switch
 *                   hardware.
 *
 * Copyright (C) 2026 Nexthop Systems Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ===========================================================================
 * MILESTONE 1: PCIe device + BAR0 register window.
 *   - Appears in `lspci`.
 *   - Decodes the Xilinx AXI-IIC register map and LOGS every read/write so you
 *     can watch a kernel driver probe it.
 *   - Returns a correct idle Status Register (SR = TX/RX FIFO empty) and
 *     handles soft reset, so the i2c-xiic probe sequence can progress.
 *
 * MILESTONE 2: real I2C transfers.
 *   - Each channel gets its own QEMU I2CBus; attach slave models from the
 *     command line
 *     (e.g. -device at24c-eeprom,bus=xiic-fpga-i2c.0,address=0x50).
 *   - Decodes the dynamic-mode DTR protocol (START 0x100 / STOP 0x200), drives
 *     the bus via i2c_start_send/i2c_start_recv/i2c_send/i2c_recv, buffers RX
 *     data for DRR reads, and sets IISR.TX_ERROR on a NACK.
 *
 * MILESTONE 3: MSI interrupt delivery (+ legacy INTx fallback).
 *   - Per-channel MSI vector (vector index == channel) so the interrupt-driven
 *     upstream i2c-xiic.c path completes transfers. Falls back to INTx when the
 *     guest has not enabled MSI. See README for the multifpgapci IRQ caveat.
 *
 * Register map per channel (offset relative to that channel's base in BAR0):
 *   DGIER  0x1C   global interrupt enable
 *   IISR   0x20   interrupt status (write-1-to-clear)
 *   IIER   0x28   interrupt enable
 *   RESETR 0x40   soft reset (write 0xA)
 *   CR     0x100  control
 *   SR     0x104  status (read-only, computed)
 *   DTR    0x108  tx data + dynamic START(0x100)/STOP(0x200)
 *   DRR    0x10C  rx data
 *   RFD    0x120  rx fifo depth
 * This matches drivers/i2c/busses/i2c-xiic.c and the Nexthop polled algo.
 * ===========================================================================
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/host-utils.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
/* qdev-properties.h moved to hw/core/ in QEMU 11.0; support both locations. */
#if defined(__has_include) && __has_include("hw/core/qdev-properties.h")
#include "hw/core/qdev-properties.h"
#else
#include "hw/qdev-properties.h"
#endif
#include "hw/i2c/i2c.h"

/* ---- AXI-IIC register offsets (relative to a channel base) ---- */
#define XIIC_DGIER_OFFSET   0x1C
#define XIIC_IISR_OFFSET    0x20
#define XIIC_IIER_OFFSET    0x28
#define XIIC_RESETR_OFFSET  0x40
#define XIIC_CR_OFFSET      0x100
#define XIIC_SR_OFFSET      0x104
#define XIIC_DTR_OFFSET     0x108
#define XIIC_DRR_OFFSET     0x10C
/* tx fifo occupancy (always 0, drains now) */
#define XIIC_TFO_OFFSET     0x114
#define XIIC_RFO_OFFSET     0x118  /* rx fifo occupancy */
#define XIIC_RFD_OFFSET     0x120

#define XIIC_RESET_MASK             0xA

/* Status register bits the drivers check. */
#define XIIC_SR_BUS_BUSY_MASK       0x04
#define XIIC_SR_RX_FIFO_FULL_MASK   0x20
#define XIIC_SR_RX_FIFO_EMPTY_MASK  0x40
#define XIIC_SR_TX_FIFO_EMPTY_MASK  0x80

/*
 * An idle controller reports both FIFOs empty. The i2c-xiic probe relies on
 * the TX bit (endianness check) and the RX bit (clear_rx_fifo loop).
 */
#define XIIC_SR_IDLE (XIIC_SR_TX_FIFO_EMPTY_MASK | XIIC_SR_RX_FIFO_EMPTY_MASK)

/*
 * Interrupt Status / Enable register bits (IISR/IIER), write-1-to-clear on
 * IISR. Values match drivers/i2c/busses/i2c-xiic.c.
 */
#define XIIC_INTR_ARB_LOST_MASK     0x01
#define XIIC_INTR_TX_ERROR_MASK     0x02  /* NACK, or (with RX_FULL) msg done */
#define XIIC_INTR_TX_EMPTY_MASK     0x04
#define XIIC_INTR_RX_FULL_MASK      0x08
/* bus not busy -> transfer complete */
#define XIIC_INTR_BNB_MASK          0x10

/* DGIER global interrupt enable bit. */
#define XIIC_GINTR_ENABLE_MASK      0x80000000UL

/* DTR dynamic-mode framing bits (the data byte is the low 8 bits). */
#define XIIC_TX_DYN_START_MASK      0x0100
#define XIIC_TX_DYN_STOP_MASK       0x0200

/*
 * RX FIFO is 16 deep on real AXI-IIC, but dynamic reads can be up to 255
 * bytes; size our software buffer to the dynamic maximum.
 */
#define XIIC_RX_FIFO_MAX            256

#define TYPE_XIIC_FPGA_I2C "xiic-fpga-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(XiicFpgaI2cState, XIIC_FPGA_I2C)

/* Per-channel AXI-IIC register + transfer state. */
typedef struct {
    uint32_t cr;
    uint32_t isr;     /* IISR */
    uint32_t ier;     /* IIER */
    uint32_t dgier;
    uint32_t rfd;

    I2CBus *bus;      /* slaves attach here */

    /* Dynamic-mode transfer state (Milestone 2). */
    bool in_xfer;     /* a START has been issued, no STOP yet */
    bool is_recv;     /* current transfer direction is read */
    bool stop_pending;/* STOP issued; raise BNB once the RX FIFO drains */
    bool irq_level;   /* last pending state, for MSI edge detection */

    uint8_t rx_fifo[XIIC_RX_FIFO_MAX];
    int rx_len;       /* bytes available */
    int rx_pos;       /* next byte to hand to a DRR read */
} XiicChannel;

struct XiicFpgaI2cState {
    PCIDevice parent_obj;

    MemoryRegion mmio;          /* BAR0 */

    /* Layout knobs (properties). Match these to the platform device JSON. */
    uint32_t num_channels;
    uint32_t ch_base_offset;    /* where channel 0 starts in BAR0 */
    uint32_t ch_stride;         /* bytes between channels */
    uint32_t bar_size;

    uint32_t num_msi_vectors;   /* >= num_channels, rounded to a power of two */

    /*
     * Top-level (FPGA-wide) interrupt block. The multifpgapci driver wires a
     * single regmap-irq chip over one STATUS register (bit N == channel N) plus
     * an UNMASK register, demuxing every channel IRQ from one shared MSI
     * vector. Offsets are properties so they can be matched to the platform's
     * use_msi JSON (interrupt_status_reg / interrupt_enable_reg).
     */
    uint32_t irq_status_offset; /* BAR offset of the per-channel status bits */
    uint32_t irq_unmask_offset; /* BAR offset of the unmask/enable register */
    uint32_t irq_msi_vector;    /* MSI vector backing the regmap-irq domain */

    uint32_t irq_unmask;        /* last value written to the unmask register */
    /* an MSI edge is outstanding (level emulation) */
    bool msi_asserted;

    XiicChannel *chan;
};

/*
 * Map a BAR offset to (channel index, register offset within channel).
 * Returns -1 if the offset is outside the channel region.
 */
static int offset_to_channel(XiicFpgaI2cState *s, hwaddr addr, hwaddr *reg)
{
    if (addr < s->ch_base_offset) {
        return -1;
    }
    hwaddr rel = addr - s->ch_base_offset;
    int idx = rel / s->ch_stride;

    if (idx >= s->num_channels) {
        return -1;
    }
    *reg = rel % s->ch_stride;
    return idx;
}

/* Is this channel asserting an enabled, globally-unmasked interrupt? */
static bool xiic_chan_pending(XiicChannel *c)
{
    if (!(c->dgier & XIIC_GINTR_ENABLE_MASK)) {
        return false;
    }
    return (c->isr & c->ier) != 0;
}

/*
 * Top-level interrupt status: bit N is set when channel N has an enabled,
 * globally-unmasked AXI-IIC interrupt. This is what the driver's regmap-irq
 * chip reads (status_base) to demux per-channel IRQs.
 */
static uint32_t xiic_compute_irq_status(XiicFpgaI2cState *s)
{
    uint32_t st = 0;
    for (unsigned i = 0; i < s->num_channels; i++) {
        if (xiic_chan_pending(&s->chan[i])) {
            st |= (1u << i);
        }
    }
    return st;
}

/*
 * Re-evaluate interrupt delivery after any isr/ier/dgier change.
 *
 * The multifpgapci driver demuxes every channel IRQ from ONE shared MSI vector
 * via a regmap-irq chip that reads the top-level status register. The driver's
 * per-channel IRQ source is *level* (asserted while IISR & IIER is set), but
 * MSI is edge, so we emulate a level source: keep an "asserted" flag and fire
 * one MSI edge while any unmasked channel is pending. The flag is cleared when
 * the regmap-irq handler acks the status register (xiic_fpga_i2c_mmio_write),
 * which re-arms delivery so a still-pending channel (e.g. BNB raised while
 * RX_FULL was being serviced) gets another edge instead of hanging. The ch
 * argument is unused; the whole top-level state is recomputed each call.
 */
static void xiic_update_irq(XiicFpgaI2cState *s, int ch)
{
    PCIDevice *pci_dev = PCI_DEVICE(s);
    uint32_t status = xiic_compute_irq_status(s);
    uint32_t active = status & s->irq_unmask;

    (void)ch;

    if (msi_enabled(pci_dev)) {
        if (active && !s->msi_asserted) {
            unsigned vec = s->irq_msi_vector < s->num_msi_vectors ?
                           s->irq_msi_vector : 0;
            qemu_log_mask(LOG_TRACE,
                          "xiic-fpga-i2c: MSI notify vec=%u status=0x%x\n",
                          vec, status);
            msi_notify(pci_dev, vec);
            s->msi_asserted = true;
        } else if (!active) {
            s->msi_asserted = false;
        }
        return;
    }

    pci_set_irq(pci_dev, active != 0);
}

static uint64_t xiic_fpga_i2c_mmio_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    XiicFpgaI2cState *s = opaque;
    hwaddr reg;
    uint64_t val = 0;

    /*
     * Top-level interrupt registers (read live so the driver's regmap-irq
     * handler always sees the current per-channel pending bits).
     */
    if (addr == s->irq_status_offset) {
        val = xiic_compute_irq_status(s);
        qemu_log_mask(LOG_TRACE,
                      "xiic-fpga-i2c: RD  IRQ_STATUS -> 0x%08" PRIx64 "\n",
                      val);
        return val;
    }
    if (addr == s->irq_unmask_offset) {
        qemu_log_mask(LOG_TRACE, "xiic-fpga-i2c: RD  IRQ_UNMASK -> 0x%08x\n",
                      s->irq_unmask);
        return s->irq_unmask;
    }

    int ch = offset_to_channel(s, addr, &reg);

    if (ch < 0) {
        qemu_log_mask(LOG_UNIMP,
                      "xiic-fpga-i2c: RD  off=0x%04" HWADDR_PRIx
                      " (outside channels) -> 0\n", addr);
        return 0;
    }

    XiicChannel *c = &s->chan[ch];
    bool rx_empty = c->rx_pos >= c->rx_len;

    switch (reg) {
    case XIIC_SR_OFFSET:
        /*
         * Transfers complete synchronously inside the DTR write, so the TX
         * FIFO always reads empty and the bus only looks busy while we are
         * mid-sequence. RX-empty reflects the software RX FIFO so the
         * clear_rx_fifo loop and the atomic/polled read path terminate.
         */
        val = XIIC_SR_TX_FIFO_EMPTY_MASK;
        if (rx_empty) {
            val |= XIIC_SR_RX_FIFO_EMPTY_MASK;
        } else {
            val |= XIIC_SR_RX_FIFO_FULL_MASK;
        }
        if (c->in_xfer) {
            val |= XIIC_SR_BUS_BUSY_MASK;
        }
        break;
    case XIIC_IISR_OFFSET:
        val = c->isr;
        break;
    case XIIC_IIER_OFFSET:
        val = c->ier;
        break;
    case XIIC_DGIER_OFFSET:
        val = c->dgier;
        break;
    case XIIC_CR_OFFSET:
        val = c->cr;
        break;
    case XIIC_RFD_OFFSET:
        val = c->rfd;
        break;
    case XIIC_RFO_OFFSET:
        /*
         * Occupancy register; the driver reads (RFO + 1) bytes from DRR, so
         * report (available - 1). Zero when the FIFO is empty.
         */
        val = rx_empty ? 0 : (c->rx_len - c->rx_pos - 1);
        break;
    case XIIC_DRR_OFFSET:
        if (!rx_empty) {
            val = c->rx_fifo[c->rx_pos++];
            if (c->rx_pos >= c->rx_len) {
                /*
                 * FIFO drained. If a STOP was framed with this read, the bus
                 * is now released: raise BNB so the driver reaches DONE.
                 */
                c->isr &= ~(uint32_t)XIIC_INTR_RX_FULL_MASK;
                if (c->stop_pending) {
                    c->stop_pending = false;
                    c->in_xfer = false;
                    c->isr |= XIIC_INTR_BNB_MASK;
                }
                xiic_update_irq(s, ch);
            }
        }
        break;
    default:
        val = 0;
        break;
    }

    qemu_log_mask(LOG_TRACE,
                  "xiic-fpga-i2c: RD  ch%d reg=0x%03" HWADDR_PRIx
                  " sz=%u -> 0x%08" PRIx64 "\n", ch, reg, size, val);
    return val;
}

/*
 * Dynamic-mode DTR write: drive the I2C bus and post interrupts (Milestone 2).
 *
 * The low 8 bits are the data byte (or, for the second write of a dynamic read,
 * the byte count). Bit 8 (START) frames the address that opens a transfer; bit
 * 9 (STOP) terminates it. Transfers complete synchronously here.
 */
static void xiic_fpga_i2c_dtr_write(XiicFpgaI2cState *s, int ch, uint64_t val)
{
    XiicChannel *c = &s->chan[ch];
    uint16_t word = val & 0xFFFF;
    bool stop = word & XIIC_TX_DYN_STOP_MASK;

    if (word & XIIC_TX_DYN_START_MASK) {
        /* START + 8-bit address (bit 0 = R/W) opens a new transfer. */
        uint8_t addr8 = word & 0xFF;
        c->is_recv = addr8 & 1;
        c->rx_len = 0;
        c->rx_pos = 0;

        int nack = c->is_recv ? i2c_start_recv(c->bus, addr8 >> 1)
                              : i2c_start_send(c->bus, addr8 >> 1);
        c->in_xfer = true;
        if (nack) {
            /* No slave ACKed the address: flag error, release the bus. */
            i2c_end_transfer(c->bus);
            c->in_xfer = false;
            c->isr |= XIIC_INTR_TX_ERROR_MASK | XIIC_INTR_BNB_MASK;
            xiic_update_irq(s, ch);
        }
        return;
    }

    if (!c->in_xfer) {
        return;  /* stray data outside a transfer; HW would drop it too */
    }

    if (c->is_recv) {
        /*
         * Dynamic read: this word is the byte count. The controller clocks
         * that many bytes from the slave into the RX FIFO autonomously.
         */
        unsigned n = MIN((unsigned)(word & 0xFF), (unsigned)XIIC_RX_FIFO_MAX);

        for (unsigned i = 0; i < n; i++) {
            c->rx_fifo[c->rx_len++] = i2c_recv(c->bus);
        }
        if (stop) {
            i2c_end_transfer(c->bus);
            c->stop_pending = true;  /* BNB raised once the FIFO is drained */
        }
        if (c->rx_len > 0) {
            c->isr |= XIIC_INTR_RX_FULL_MASK;
        }
        xiic_update_irq(s, ch);
        return;
    }

    /* Dynamic write: low byte is data; STOP terminates the message. */
    if (i2c_send(c->bus, word & 0xFF)) {
        i2c_end_transfer(c->bus);
        c->in_xfer = false;
        c->isr |= XIIC_INTR_TX_ERROR_MASK | XIIC_INTR_BNB_MASK;
        xiic_update_irq(s, ch);
        return;
    }
    if (stop) {
        i2c_end_transfer(c->bus);
        c->in_xfer = false;
        c->isr |= XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_BNB_MASK;
    } else {
        /*
         * FIFO drained instantly; invite the driver to push more bytes
         * (writes longer than the hardware FIFO depth rely on this).
         */
        c->isr |= XIIC_INTR_TX_EMPTY_MASK;
    }
    xiic_update_irq(s, ch);
}

static void xiic_fpga_i2c_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    XiicFpgaI2cState *s = opaque;
    hwaddr reg;

    /*
     * Top-level interrupt registers. Status is computed live from the channels,
     * so a write (regmap-irq ack) is a harmless no-op: the bit clears when the
     * channel's IISR is cleared. The unmask register is stored so the driver's
     * regmap-irq sync_unlock reads back what it wrote.
     */
    if (addr == s->irq_status_offset) {
        qemu_log_mask(LOG_TRACE,
                      "xiic-fpga-i2c: WR  IRQ_STATUS(ack) <- 0x%08" PRIx64 "\n",
                      val);
        /*
         * regmap-irq acks here after running the channel handler. Drop the
         * outstanding MSI edge and re-arm: if a channel is still pending (a new
         * IISR cause raised during handling), this delivers another edge.
         */
        s->msi_asserted = false;
        xiic_update_irq(s, -1);
        return;
    }
    if (addr == s->irq_unmask_offset) {
        s->irq_unmask = val;
        qemu_log_mask(LOG_TRACE,
                      "xiic-fpga-i2c: WR  IRQ_UNMASK <- 0x%08" PRIx64 "\n",
                      val);
        return;
    }

    int ch = offset_to_channel(s, addr, &reg);

    if (ch < 0) {
        qemu_log_mask(LOG_UNIMP,
                      "xiic-fpga-i2c: WR  off=0x%04" HWADDR_PRIx
                      " (outside channels) <- 0x%08" PRIx64 "\n", addr, val);
        return;
    }

    XiicChannel *c = &s->chan[ch];

    qemu_log_mask(LOG_TRACE,
                  "xiic-fpga-i2c: WR  ch%d reg=0x%03" HWADDR_PRIx
                  " sz=%u <- 0x%08" PRIx64 "\n", ch, reg, size, val);

    switch (reg) {
    case XIIC_RESETR_OFFSET:
        if ((val & 0xf) == XIIC_RESET_MASK) {
            /* Soft reset: abandon any transfer and clear interrupt state. */
            if (c->in_xfer) {
                i2c_end_transfer(c->bus);
            }
            c->isr = 0;
            c->cr = 0;
            c->in_xfer = false;
            c->is_recv = false;
            c->stop_pending = false;
            c->rx_len = 0;
            c->rx_pos = 0;
            xiic_update_irq(s, ch);
        }
        break;
    case XIIC_CR_OFFSET:
        c->cr = val;
        break;
    case XIIC_DGIER_OFFSET:
        c->dgier = val;
        xiic_update_irq(s, ch);
        break;
    case XIIC_IIER_OFFSET:
        c->ier = val;
        xiic_update_irq(s, ch);
        break;
    case XIIC_IISR_OFFSET:
        c->isr &= ~(uint32_t)val;            /* write-1-clear */
        xiic_update_irq(s, ch);
        break;
    case XIIC_RFD_OFFSET:
        c->rfd = val;
        break;
    case XIIC_DTR_OFFSET:
        xiic_fpga_i2c_dtr_write(s, ch, val);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps xiic_fpga_i2c_mmio_ops = {
    .read = xiic_fpga_i2c_mmio_read,
    .write = xiic_fpga_i2c_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void xiic_fpga_i2c_realize(PCIDevice *pci_dev, Error **errp)
{
    XiicFpgaI2cState *s = XIIC_FPGA_I2C(pci_dev);

    if (s->num_channels < 1) {
        error_setg(errp, "num_channels must be >= 1");
        return;
    }
    /*
     * Grow BAR0 if the channel layout or the interrupt registers need more
     * room (the IRQ status/unmask registers live at their own BAR offsets).
     */
    if (s->bar_size < s->ch_base_offset + s->num_channels * s->ch_stride) {
        s->bar_size = s->ch_base_offset + s->num_channels * s->ch_stride;
    }
    if (s->bar_size < s->irq_status_offset + 4) {
        s->bar_size = s->irq_status_offset + 4;
    }
    if (s->bar_size < s->irq_unmask_offset + 4) {
        s->bar_size = s->irq_unmask_offset + 4;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &xiic_fpga_i2c_mmio_ops, s,
                          "xiic-fpga-i2c-bar0", s->bar_size);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    s->chan = g_new0(XiicChannel, s->num_channels);

    /*
     * One I2C bus per channel. The bus name lets slaves attach from the
     * command line, e.g. -device at24c-eeprom,bus=xiic-fpga-i2c.0,address=0x50
     */
    for (unsigned i = 0; i < s->num_channels; i++) {
        g_autofree char *name = g_strdup_printf("xiic-fpga-i2c.%u", i);
        s->chan[i].bus = i2c_init_bus(DEVICE(s), name);
    }

    /*
     * MSI: one vector per channel (vector index == channel), rounded up to a
     * power of two as the MSI capability requires. msi_init caps at 32.
     */
    s->num_msi_vectors = pow2ceil(s->num_channels);
    if (s->num_msi_vectors > 32) {
        s->num_msi_vectors = 32;
    }
    if (msi_init(pci_dev, 0, s->num_msi_vectors, true, false, errp) < 0) {
        error_prepend(errp, "xiic-fpga-i2c: failed to init MSI: ");
        return;
    }

    qemu_log_mask(LOG_TRACE,
                  "xiic-fpga-i2c: realized bar0=0x%x channels=%u base=0x%x "
                  "stride=0x%x msi-vectors=%u\n", s->bar_size, s->num_channels,
                  s->ch_base_offset, s->ch_stride, s->num_msi_vectors);
}

static void xiic_fpga_i2c_exit(PCIDevice *pci_dev)
{
    XiicFpgaI2cState *s = XIIC_FPGA_I2C(pci_dev);
    msi_uninit(pci_dev);
    g_free(s->chan);
}

static const Property xiic_fpga_i2c_props[] = {
    DEFINE_PROP_UINT32("num-channels", XiicFpgaI2cState, num_channels, 4),
    DEFINE_PROP_UINT32("ch-base-offset", XiicFpgaI2cState, ch_base_offset, 0x0),
    DEFINE_PROP_UINT32("ch-stride", XiicFpgaI2cState, ch_stride, 0x1000),
    DEFINE_PROP_UINT32("bar-size", XiicFpgaI2cState, bar_size, 0x8000),
    /*
     * Top-level interrupt block. Defaults sit above a 4x0x1000 channel layout;
     * match them to the platform use_msi JSON (interrupt_status_reg /
     * interrupt_enable_reg) and the regmap-irq vector.
     */
    DEFINE_PROP_UINT32("irq-status-offset", XiicFpgaI2cState,
                       irq_status_offset, 0x6000),
    DEFINE_PROP_UINT32("irq-unmask-offset", XiicFpgaI2cState,
                       irq_unmask_offset, 0x6004),
    DEFINE_PROP_UINT32("irq-msi-vector", XiicFpgaI2cState, irq_msi_vector, 0),
};

static void xiic_fpga_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize   = xiic_fpga_i2c_realize;
    k->exit      = xiic_fpga_i2c_exit;
    /*
     * PCI ids the guest sees in lspci. The kernel multifpgapci driver learns
     * its match id from the platform JSON at runtime, so just keep the JSON's
     * vendor/device equal to these two numbers. (Xilinx vendor id by default.)
     */
    k->vendor_id = 0x10ee;
    k->device_id = 0x7021;
    k->revision  = 0x01;
    k->class_id  = PCI_CLASS_OTHERS;

    dc->desc = "Nexthop FPGA I2C (Xilinx AXI-IIC) emulation";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, xiic_fpga_i2c_props);
}

static const TypeInfo xiic_fpga_i2c_info = {
    .name          = TYPE_XIIC_FPGA_I2C,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XiicFpgaI2cState),
    .class_init    = xiic_fpga_i2c_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void xiic_fpga_i2c_register_types(void)
{
    type_register_static(&xiic_fpga_i2c_info);
}

type_init(xiic_fpga_i2c_register_types)
