/*
 * Synopsys DesignWare SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Emulates the DesignWare SSI controller in Standard SPI mode,
 * covering the PIO/FIFO data path, interrupt outputs and cs and
 * serial-slave is not modelled.
 */

#include "qemu/osdep.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/ssi/dwc_ssi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* DWC SSI 1.03 derivative */
#define DWC_SSI_CTRLR0_RESET            0x00004007
#define DWC_SSI_SR_RESET                0x00000006
#define DWC_SSI_IDR_RESET               0xa1b2c3d5
#define DWC_SSI_VERSION                 0x3130332a
#define DWC_SSI_IMR_RESET_BASE          0x0000001f
#define DWC_SSI_IRQ_VALID_MASK          0x000009bf
#define DWC_SSI_IMPLEMENTED_LATCHED_IRQ_MASK \
    (R_RISR_TXOIR_MASK | R_RISR_RXOIR_MASK | R_RISR_RXUIR_MASK)

enum {
    DWC_SSI_TMOD_TR,
    DWC_SSI_TMOD_TO,
    DWC_SSI_TMOD_RO,
    DWC_SSI_TMOD_EEPROM_READ,
};

REG32(CTRLR0, 0x000)
    FIELD(CTRLR0, DFS, 0, 5)
    FIELD(CTRLR0, FRF, 6, 2)
    FIELD(CTRLR0, SCPH, 8, 1)
    FIELD(CTRLR0, SCPOL, 9, 1)
    FIELD(CTRLR0, TMOD, 10, 2)
    FIELD(CTRLR0, SLV_OE, 12, 1)
    FIELD(CTRLR0, SRL, 13, 1)
    FIELD(CTRLR0, SSTE, 14, 1)
    FIELD(CTRLR0, CFS, 16, 4)
    FIELD(CTRLR0, SPI_FRF, 22, 2)
    FIELD(CTRLR0, SPI_HYPERBUS_EN, 24, 1)
    FIELD(CTRLR0, SPI_DWS_EN, 25, 1)
REG32(CTRLR1, 0x004)
    FIELD(CTRLR1, NDF, 0, 16)
REG32(SSIENR, 0x008)
    FIELD(SSIENR, SSIC_EN, 0, 1)
REG32(MWCR, 0x00c)
    FIELD(MWCR, MWMOD, 0, 1)
    FIELD(MWCR, MDD, 1, 1)
    FIELD(MWCR, MHS, 2, 1)
REG32(SER, 0x010)
REG32(BAUDR, 0x014)
    FIELD(BAUDR, SCKDV, 1, 15)
REG32(TXFTLR, 0x018)
    FIELD(TXFTLR, TFT, 0, 8)
    FIELD(TXFTLR, TXFTHR, 16, 11)
REG32(RXFTLR, 0x01c)
    FIELD(RXFTLR, RFT, 0, 8)
REG32(TXFLR, 0x020)
    FIELD(TXFLR, TXTFL, 0, 9)
REG32(RXFLR, 0x024)
    FIELD(RXFLR, RXTFL, 0, 9)
REG32(SR, 0x028)
    FIELD(SR, CMPLTD_DF, 15, 17)
    FIELD(SR, DCOL, 6, 1)
    FIELD(SR, TXE, 5, 1)
    FIELD(SR, BUSY, 0, 1)
    FIELD(SR, TFNF, 1, 1)
    FIELD(SR, TFE, 2, 1)
    FIELD(SR, RFNE, 3, 1)
    FIELD(SR, RFF, 4, 1)
REG32(IMR, 0x02c)
    FIELD(IMR, DONEM, 11, 1)
    FIELD(IMR, SPITEM, 10, 1)
    FIELD(IMR, AXIEM, 8, 1)
    FIELD(IMR, TXUIM, 7, 1)
    FIELD(IMR, XRXOIM, 6, 1)
    FIELD(IMR, MSTIM, 5, 1)
    FIELD(IMR, RXOIM, 3, 1)
    FIELD(IMR, RXUIM, 2, 1)
    FIELD(IMR, TXOIM, 1, 1)
    FIELD(IMR, TXEIM, 0, 1)
    FIELD(IMR, RXFIM, 4, 1)
REG32(ISR, 0x030)
    FIELD(ISR, DONES, 11, 1)
    FIELD(ISR, SPITES, 10, 1)
    FIELD(ISR, AXIES, 8, 1)
    FIELD(ISR, TXUIS, 7, 1)
    FIELD(ISR, XRXOIS, 6, 1)
    FIELD(ISR, MSTIS, 5, 1)
    FIELD(ISR, RXOIS, 3, 1)
    FIELD(ISR, RXUIS, 2, 1)
    FIELD(ISR, TXOIS, 1, 1)
    FIELD(ISR, TXEIS, 0, 1)
    FIELD(ISR, RXFIS, 4, 1)
REG32(RISR, 0x034)
    FIELD(RISR, DONER, 11, 1)
    FIELD(RISR, SPITER, 10, 1)
    FIELD(RISR, AXIER, 8, 1)
    FIELD(RISR, TXUIR, 7, 1)
    FIELD(RISR, XRXOIR, 6, 1)
    FIELD(RISR, MSTIR, 5, 1)
    FIELD(RISR, RXOIR, 3, 1)
    FIELD(RISR, RXUIR, 2, 1)
    FIELD(RISR, TXOIR, 1, 1)
    FIELD(RISR, TXEIR, 0, 1)
    FIELD(RISR, RXFIR, 4, 1)
REG32(TXEICR, 0x038)
    FIELD(TXEICR, TXEICR, 0, 1)
REG32(RXOICR, 0x03c)
    FIELD(RXOICR, RXOICR, 0, 1)
REG32(RXUICR, 0x040)
    FIELD(RXUICR, RXUICR, 0, 1)
REG32(MSTICR, 0x044)
    FIELD(MSTICR, MSTICR, 0, 1)
REG32(ICR, 0x048)
    FIELD(ICR, ICR, 0, 1)
REG32(DMACR, 0x04c)
    FIELD(DMACR, RDMAE, 0, 1)
    FIELD(DMACR, TDMAE, 1, 1)
    FIELD(DMACR, IDMAE, 2, 1)
    FIELD(DMACR, ATW, 3, 2)
    FIELD(DMACR, AINC, 6, 1)
    FIELD(DMACR, ACACHE, 8, 4)
    FIELD(DMACR, APROT, 12, 3)
    FIELD(DMACR, AID, 15, 4)
REG32(AXIAWLEN, 0x050)
    FIELD(AXIAWLEN, AWLEN, 8, 8)
REG32(AXIARLEN, 0x054)
    FIELD(AXIARLEN, ARLEN, 8, 8)
REG32(IDR, 0x058)
    FIELD(IDR, IDCODE, 0, 32)
REG32(SSIC_VERSION_ID, 0x05c)
    FIELD(SSIC_VERSION_ID, VERSION_ID, 0, 32)
REG32(DR0, 0x060)
REG32(DR_END, 0x0ec)
REG32(RX_SAMPLE_DELAY, 0x0f0)
    FIELD(RX_SAMPLE_DELAY, RSD, 0, 8)
    FIELD(RX_SAMPLE_DELAY, SE, 16, 1)
REG32(SPI_CTRLR0, 0x0f4)
    FIELD(SPI_CTRLR0, CLK_STRETCH_EN, 30, 1)
    FIELD(SPI_CTRLR0, XIP_PREFETCH_EN, 29, 1)
    FIELD(SPI_CTRLR0, XIP_MBL, 26, 2)
    FIELD(SPI_CTRLR0, SPI_RXDS_SIG_EN, 25, 1)
    FIELD(SPI_CTRLR0, SPI_DM_EN, 24, 1)
    FIELD(SPI_CTRLR0, SSIC_XIP_CONT_XFER_EN, 21, 1)
    FIELD(SPI_CTRLR0, XIP_INST_EN, 20, 1)
    FIELD(SPI_CTRLR0, XIP_DFS_HC, 19, 1)
    FIELD(SPI_CTRLR0, SPI_RXDS_EN, 18, 1)
    FIELD(SPI_CTRLR0, INST_DDR_EN, 17, 1)
    FIELD(SPI_CTRLR0, SPI_DDR_EN, 16, 1)
    FIELD(SPI_CTRLR0, WAIT_CYCLES, 11, 5)
    FIELD(SPI_CTRLR0, INST_L, 8, 2)
    FIELD(SPI_CTRLR0, XIP_MD_BIT_EN, 7, 1)
    FIELD(SPI_CTRLR0, ADDR_L, 2, 4)
    FIELD(SPI_CTRLR0, TRANS_TYPE, 0, 2)
REG32(DDR_DRIVE_EDGE, 0x0f8)
    FIELD(DDR_DRIVE_EDGE, TDE, 0, 8)
REG32(XIP_MODE_BITS, 0x0fc)
    FIELD(XIP_MODE_BITS, XIP_MD_BITS, 0, 16)
REG32(XIP_INCR_INST, 0x100)
    FIELD(XIP_INCR_INST, INCR_INST, 0, 16)
REG32(XIP_WRAP_INST, 0x104)
    FIELD(XIP_WRAP_INST, WRAP_INST, 0, 16)
REG32(XIP_CTRL, 0x108)
REG32(XIP_SER, 0x10c)
REG32(XRXOICR, 0x110)
REG32(XIP_CNT_TIME_OUT, 0x114)
REG32(SPI_CTRLR1, 0x118)
REG32(SPITECR, 0x11c)
REG32(SPIDR, 0x120)
    FIELD(SPIDR, SPI_INST, 0, 16)
REG32(SPIAR, 0x124)
    FIELD(SPIAR, SDAR, 0, 32)
REG32(AXIAR0, 0x128)
    FIELD(AXIAR0, AXIAR_0_31, 0, 32)
REG32(AXIAR1, 0x12c)
    FIELD(AXIAR1, AXIAR_32_63, 0, 32)
REG32(AXIECR, 0x130)
    FIELD(AXIECR, AXIECR, 0, 1)
REG32(DONECR, 0x134)
    FIELD(DONECR, DONECR, 0, 1)
REG32(RSVD_138, 0x138)
REG32(RSVD_13C, 0x13c)
REG32(XIP_WRITE_INCR_INST, 0x140)
REG32(XIP_WRITE_WRAP_INST, 0x144)
REG32(XIP_WRITE_CTRL, 0x148)

#define DWC_SSI_CTRLR0_STANDARD_WRITABLE_MASK \
    (R_CTRLR0_DFS_MASK | \
     R_CTRLR0_SCPH_MASK | \
     R_CTRLR0_SCPOL_MASK | \
     R_CTRLR0_TMOD_MASK | \
     R_CTRLR0_SLV_OE_MASK | \
     R_CTRLR0_SRL_MASK | \
     R_CTRLR0_SSTE_MASK | \
     R_CTRLR0_CFS_MASK)

#define DWC_SSI_CTRLR1_WRITABLE_MASK R_CTRLR1_NDF_MASK
#define DWC_SSI_MWCR_WRITABLE_MASK \
    (R_MWCR_MWMOD_MASK | R_MWCR_MDD_MASK | R_MWCR_MHS_MASK)
#define DWC_SSI_BAUDR_WRITABLE_MASK R_BAUDR_SCKDV_MASK
#define DWC_SSI_TXFTLR_WRITABLE_MASK \
    (R_TXFTLR_TFT_MASK | R_TXFTLR_TXFTHR_MASK)
#define DWC_SSI_RXFTLR_WRITABLE_MASK R_RXFTLR_RFT_MASK
#define DWC_SSI_IMR_WRITABLE_MASK \
    (R_IMR_TXEIM_MASK | R_IMR_TXOIM_MASK | R_IMR_RXUIM_MASK | \
     R_IMR_RXOIM_MASK | R_IMR_RXFIM_MASK | R_IMR_MSTIM_MASK | \
     R_IMR_TXUIM_MASK)
#define DWC_SSI_RX_SAMPLE_DELAY_WRITABLE_MASK \
    (R_RX_SAMPLE_DELAY_RSD_MASK | R_RX_SAMPLE_DELAY_SE_MASK)

/*
 * TXU/DONE/AXIE: not modeled in Standard PIO.
 * Just wired out and tied low.
 */
static const uint32_t dwc_ssi_irq_status_mask[DWC_SSI_IRQ_COUNT] = {
    [DWC_SSI_IRQ_TXE] = R_RISR_TXEIR_MASK,
    [DWC_SSI_IRQ_TXO] = R_RISR_TXOIR_MASK,
    [DWC_SSI_IRQ_RXF] = R_RISR_RXFIR_MASK,
    [DWC_SSI_IRQ_RXO] = R_RISR_RXOIR_MASK,
    [DWC_SSI_IRQ_TXU] = R_RISR_TXUIR_MASK,
    [DWC_SSI_IRQ_RXU] = R_RISR_RXUIR_MASK,
    [DWC_SSI_IRQ_MST] = R_RISR_MSTIR_MASK,
    [DWC_SSI_IRQ_DONE] = R_RISR_DONER_MASK,
    [DWC_SSI_IRQ_AXIE] = R_RISR_AXIER_MASK,
};

static bool dwc_ssi_validate_config(DwcSsiState *s, Error **errp)
{
    DeviceState *dev = DEVICE(s);

    if (s->cfg.num_cs == 0 || s->cfg.num_cs > 8) {
        error_setg(errp, "%s: num-cs must be in range 1..8",
                   dev->canonical_path);
        return false;
    }

    if (s->cfg.fifo_depth < 2 || s->cfg.fifo_depth > 256) {
        error_setg(errp, "%s: fifo-depth must be in range 2..256",
                   dev->canonical_path);
        return false;
    }

    return true;
}

static uint32_t dwc_ssi_imr_reset(const DwcSsiState *s)
{
    /* Select the profile-specific IMR reset value */
    return DWC_SSI_IMR_RESET_BASE |
           (s->cfg.master_mode ? R_IMR_MSTIM_MASK : 0);
}

static bool dwc_ssi_fifo_threshold_valid(const DwcSsiState *s, uint32_t value)
{
    return value < s->cfg.fifo_depth;
}

static void dwc_ssi_write_masked(DwcSsiState *s, unsigned int reg,
                                 uint32_t value, uint32_t mask)
{
    s->regs[reg] = (s->regs[reg] & ~mask) | (value & mask);
}

static uint32_t dwc_ssi_irq_raw_status(DwcSsiState *s)
{
    uint32_t status = s->irq_latched;
    uint32_t tx_used = fifo32_num_used(&s->tx_fifo);
    uint32_t rx_used = fifo32_num_used(&s->rx_fifo);
    uint32_t tx_threshold =
        FIELD_EX32(s->regs[R_TXFTLR], TXFTLR, TFT);
    uint32_t rx_threshold =
        FIELD_EX32(s->regs[R_RXFTLR], RXFTLR, RFT);

    if (tx_used <= tx_threshold) {
        status |= R_RISR_TXEIR_MASK;
    }
    if (rx_used > rx_threshold) {
        status |= R_RISR_RXFIR_MASK;
    }
    return status & DWC_SSI_IRQ_VALID_MASK;
}

static void dwc_ssi_update_irq(DwcSsiState *s)
{
    uint32_t status = dwc_ssi_irq_raw_status(s) &
                      s->regs[R_IMR] & DWC_SSI_IRQ_VALID_MASK;

    for (int i = 0; i < DWC_SSI_IRQ_COUNT; i++) {
        qemu_set_irq(s->irqs[i], !!(status & dwc_ssi_irq_status_mask[i]));
    }
}

static uint32_t dwc_ssi_irq_read_clear(DwcSsiState *s,
                                       uint32_t clear_mask)
{
    uint32_t active = s->irq_latched & clear_mask;

    s->irq_latched &= ~clear_mask;
    dwc_ssi_update_irq(s);
    return !!active;
}

static uint32_t dwc_ssi_frame_masked(DwcSsiState *s)
{
    unsigned int bits = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, DFS) + 1;

    return bits == 32 ? UINT32_MAX : MAKE_64BIT_MASK(0, bits);
}

static bool dwc_ssi_is_enabled(const DwcSsiState *s)
{
    return FIELD_EX32(s->regs[R_SSIENR], SSIENR, SSIC_EN);
}

static void dwc_ssi_deselect(DwcSsiState *s)
{
    if (s->active_cs < 0) {
        return;
    }

    qemu_irq_raise(s->cs_lines[s->active_cs]);
    s->active_cs = -1;
}

static void dwc_ssi_select(DwcSsiState *s, unsigned cs)
{
    if (cs >= s->cfg.num_cs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid cs %u\n",
                      DEVICE(s)->canonical_path, cs);
        dwc_ssi_deselect(s);
        return;
    }

    if (s->active_cs == cs) {
        return;
    }

    dwc_ssi_deselect(s);
    qemu_irq_lower(s->cs_lines[cs]);
    s->active_cs = cs;
}

static void dwc_ssi_update_cs(DwcSsiState *s)
{
    uint32_t ser = s->regs[R_SER];

    if (!dwc_ssi_is_enabled(s) || !ser) {
        dwc_ssi_deselect(s);
        return;
    }

    if (ser & (ser - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: multiple cs enabled: 0x%x\n",
                      DEVICE(s)->canonical_path, ser);
        dwc_ssi_deselect(s);
        return;
    }

    dwc_ssi_select(s, ctz32(ser));
}

static void dwc_ssi_abort_transfer(DwcSsiState *s)
{
    dwc_ssi_deselect(s);
    fifo32_reset(&s->tx_fifo);
    fifo32_reset(&s->rx_fifo);
    s->phase = DWC_SSI_PHASE_IDLE;
    s->remaining_frames = 0;
    s->dummy_frame = 0;
    dwc_ssi_update_irq(s);
}

static uint32_t dwc_ssi_status(DwcSsiState *s)
{
    uint32_t tx_used = fifo32_num_used(&s->tx_fifo);
    uint32_t rx_used = fifo32_num_used(&s->rx_fifo);
    uint32_t sr = 0;

    sr = FIELD_DP32(sr, SR, BUSY, s->phase != DWC_SSI_PHASE_IDLE);
    sr = FIELD_DP32(sr, SR, TFNF, tx_used < s->cfg.fifo_depth);
    sr = FIELD_DP32(sr, SR, TFE, tx_used == 0);
    sr = FIELD_DP32(sr, SR, RFNE, rx_used != 0);
    sr = FIELD_DP32(sr, SR, RFF,
                    rx_used == s->cfg.fifo_depth);

    return sr;
}

static void dwc_ssi_run_transfer(DwcSsiState *s);

static void dwc_ssi_push_tx(DwcSsiState *s, uint32_t tx)
{
    if (!dwc_ssi_is_enabled(s)) {
        return;
    }

    if (fifo32_is_full(&s->tx_fifo)) {
        s->irq_latched |= R_RISR_TXOIR_MASK;
        dwc_ssi_update_irq(s);
        return;
    }

    fifo32_push(&s->tx_fifo, tx);
    dwc_ssi_run_transfer(s);
    dwc_ssi_update_irq(s);
}

static uint32_t dwc_ssi_send_frame(DwcSsiState *s,
                                   uint32_t tx)
{
    uint32_t mask = dwc_ssi_frame_masked(s);
    uint32_t rx;

    tx &= mask;

    /*
     * For frame widths greater than 8 bits, 8-bit device models such as
     * m25p80 consume only the lower 8 bits; the upper bits are ignored.
     */
    if (mask > 0xff) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: frames wider than 8 bits are passed to the "
                      "bus as-is; 8-bit peripherals drop the high bits\n",
                      DEVICE(s)->canonical_path);
    }

    if (FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, SRL)) {
        rx = tx;
    } else {
        rx = ssi_transfer(s->spi, tx);
    }

    return rx & mask;
}

static void dwc_ssi_run_transfer(DwcSsiState *s)
{
    uint32_t tmod;

    if (!dwc_ssi_is_enabled(s) || s->active_cs < 0) {
        return;
    }

    /* SCKDV=0 disables sclk_out: no transfer can start. */
    if (FIELD_EX32(s->regs[R_BAUDR], BAUDR, SCKDV) == 0) {
        return;
    }

    /*
     * Shift doesn't start until TX FIFO > TXFTLR.TXFTHR.
     * In-flight transfers won't be aborted if FIFO drops below.
     */
    if (s->phase == DWC_SSI_PHASE_IDLE &&
        fifo32_num_used(&s->tx_fifo) <=
            FIELD_EX32(s->regs[R_TXFTLR], TXFTLR, TXFTHR)) {
        return;
    }

    tmod = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, TMOD);

    switch (tmod) {
    case DWC_SSI_TMOD_TR:
        if (s->phase == DWC_SSI_PHASE_IDLE) {
            s->phase = DWC_SSI_PHASE_STANDARD_TR;
        }

        while (!fifo32_is_empty(&s->tx_fifo)) {
            uint32_t tx = fifo32_pop(&s->tx_fifo);
            uint32_t rx = dwc_ssi_send_frame(s, tx);

            if (fifo32_is_full(&s->rx_fifo)) {
                s->irq_latched |= R_RISR_RXOIR_MASK;
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: RX FIFO full, dropping frame\n",
                              DEVICE(s)->canonical_path);
            } else {
                fifo32_push(&s->rx_fifo, rx);
            }
        }

        if (fifo32_is_empty(&s->tx_fifo)) {
            s->phase = DWC_SSI_PHASE_IDLE;
        }
        break;
    case DWC_SSI_TMOD_TO:
        if (fifo32_is_empty(&s->tx_fifo)) {
            s->phase = DWC_SSI_PHASE_IDLE;
            break;
        }

        s->phase = DWC_SSI_PHASE_STANDARD_TX_ONLY;
        while (!fifo32_is_empty(&s->tx_fifo)) {
            uint32_t tx = fifo32_pop(&s->tx_fifo);

            dwc_ssi_send_frame(s, tx);
        }
        s->phase = DWC_SSI_PHASE_IDLE;
        break;
    case DWC_SSI_TMOD_RO:
        switch (s->phase) {
        case DWC_SSI_PHASE_IDLE:
            if (fifo32_is_empty(&s->tx_fifo)) {
                break;
            }
            s->dummy_frame = fifo32_pop(&s->tx_fifo);
            s->phase = DWC_SSI_PHASE_RX_ONLY;
            s->remaining_frames = FIELD_EX32(s->regs[R_CTRLR1], CTRLR1,
                                              NDF) + 1;
            /* Fall through */
        case DWC_SSI_PHASE_RX_ONLY:
            while (s->remaining_frames > 0 &&
                   !fifo32_is_full(&s->rx_fifo)) {
                uint32_t rx = dwc_ssi_send_frame(s, s->dummy_frame);

                fifo32_push(&s->rx_fifo, rx);
                s->remaining_frames--;
            }
            if (s->remaining_frames == 0) {
                s->phase = DWC_SSI_PHASE_IDLE;
            }
            break;
        }
        break;
    case DWC_SSI_TMOD_EEPROM_READ:
        switch (s->phase) {
        case DWC_SSI_PHASE_IDLE:
            if (fifo32_is_empty(&s->tx_fifo)) {
                break;
            }
            s->phase = DWC_SSI_PHASE_EEPROM_COMMAND;
            /* Fall through */
        case DWC_SSI_PHASE_EEPROM_COMMAND:
            if (fifo32_is_empty(&s->tx_fifo)) {
                break;
            }
            while (!fifo32_is_empty(&s->tx_fifo)) {
                uint32_t tx = fifo32_pop(&s->tx_fifo);

                dwc_ssi_send_frame(s, tx);
            }
            s->phase = DWC_SSI_PHASE_EEPROM_DATA;
            s->remaining_frames = FIELD_EX32(s->regs[R_CTRLR1], CTRLR1,
                                              NDF) + 1;
            /* Fall through */
        case DWC_SSI_PHASE_EEPROM_DATA:
            while (s->remaining_frames > 0 &&
                   !fifo32_is_full(&s->rx_fifo)) {
                uint32_t rx = dwc_ssi_send_frame(s, 0x00);

                fifo32_push(&s->rx_fifo, rx);
                s->remaining_frames--;
            }
            if (s->remaining_frames == 0) {
                s->phase = DWC_SSI_PHASE_IDLE;
            }
            break;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static bool dwc_ssi_is_dr(hwaddr addr)
{
    return addr >= A_DR0 && addr <= A_DR_END &&
           (addr & 0x3) == 0;
}

static bool dwc_ssi_is_unsupported_offset(hwaddr addr)
{
    switch (addr) {
    /* Enhanced SPI extensions */
    case A_SPI_CTRLR0:
    case A_DDR_DRIVE_EDGE:

    /* XIP extensions */
    case A_XIP_MODE_BITS:
    case A_XIP_INCR_INST:
    case A_XIP_WRAP_INST:
    case A_XIP_CTRL:
    case A_XIP_SER:
    case A_XRXOICR:
    case A_XIP_CNT_TIME_OUT:
    case A_SPI_CTRLR1:
    case A_SPITECR:
    case A_RSVD_138:
    case A_RSVD_13C:
    case A_XIP_WRITE_INCR_INST:
    case A_XIP_WRITE_WRAP_INST:
    case A_XIP_WRITE_CTRL:

    /* Internal-AXI DMA/IDMA extensions */
    case A_DMACR:
    case A_AXIAWLEN:
    case A_AXIARLEN:
    case A_SPIDR:
    case A_SPIAR:
    case A_AXIAR0:
    case A_AXIAR1:
    case A_AXIECR:
    case A_DONECR:

    /* SSI unsupported extensions */
    case A_RX_SAMPLE_DELAY:
        return true;
    default:
        return false;
    }
}

static bool dwc_ssi_write_requires_disabled(hwaddr addr)
{
    switch (addr) {
    case A_CTRLR0:
    case A_CTRLR1:
    case A_MWCR:
    case A_BAUDR:
        return true;
    default:
        return false;
    }
}

static uint32_t dwc_ssi_unsupported_read(DwcSsiState *s,
                                        hwaddr addr)
{
    qemu_log_mask(LOG_UNIMP,
                  "%s: unsupported read at 0x%" HWADDR_PRIx "\n",
                  DEVICE(s)->canonical_path, addr);
    return 0;
}

static void dwc_ssi_unsupported_write(DwcSsiState *s,
                                     hwaddr addr,
                                     uint32_t value)
{
    qemu_log_mask(LOG_UNIMP,
                  "%s: unsupported write at 0x%" HWADDR_PRIx
                  " value 0x%08x\n",
                  DEVICE(s)->canonical_path, addr, value);
}

static uint64_t dwc_ssi_read(void *opaque, hwaddr addr, unsigned int size)
{
    DwcSsiState *s = DWC_SSI(opaque);
    uint32_t value = 0;

    if (dwc_ssi_is_dr(addr)) {
        if (!fifo32_is_empty(&s->rx_fifo)) {
            value = fifo32_pop(&s->rx_fifo) & dwc_ssi_frame_masked(s);
        } else {
            value = 0;
            s->irq_latched |= R_RISR_RXUIR_MASK;
        }

        dwc_ssi_run_transfer(s);
        dwc_ssi_update_irq(s);
        return value;
    }

    if (dwc_ssi_is_unsupported_offset(addr)) {
        value = dwc_ssi_unsupported_read(s, addr);
        return value;
    }

    switch (addr) {
    case A_CTRLR0:
    case A_CTRLR1:
    case A_SSIENR:
    case A_MWCR:
    case A_BAUDR:
    case A_TXFTLR:
    case A_RXFTLR:
    case A_IMR:
    case A_IDR:
    case A_SSIC_VERSION_ID:
        value = s->regs[addr / sizeof(uint32_t)];
        break;

    case A_SER:
        value = s->regs[R_SER] & MAKE_64BIT_MASK(0, s->cfg.num_cs);
        break;
    case A_TXFLR:
        value = fifo32_num_used(&s->tx_fifo);
        break;
    case A_RXFLR:
        value = fifo32_num_used(&s->rx_fifo);
        break;
    case A_SR:
        value = dwc_ssi_status(s);
        break;
    case A_ISR:
        value = dwc_ssi_irq_raw_status(s) & s->regs[R_IMR] &
                DWC_SSI_IRQ_VALID_MASK;
        break;
    case A_RISR:
        value = dwc_ssi_irq_raw_status(s);
        break;
    case A_TXEICR:
        value = dwc_ssi_irq_read_clear(
            s, R_RISR_TXOIR_MASK | R_RISR_TXUIR_MASK);
        break;
    case A_RXOICR:
        value = dwc_ssi_irq_read_clear(s, R_RISR_RXOIR_MASK);
        break;
    case A_RXUICR:
        value = dwc_ssi_irq_read_clear(s, R_RISR_RXUIR_MASK);
        break;
    case A_MSTICR:
        value = dwc_ssi_irq_read_clear(s, R_RISR_MSTIR_MASK);
        break;
    case A_ICR:
        value = dwc_ssi_irq_read_clear(
            s, R_RISR_TXOIR_MASK | R_RISR_RXUIR_MASK |
               R_RISR_RXOIR_MASK | R_RISR_MSTIR_MASK);
        break;

    default:
        if (addr >= DWC_SSI_REGS_SIZE || (addr & 0x3) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                          DEVICE(s)->canonical_path, addr);
        }
        break;
    }

    return value;
}

static void dwc_ssi_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned int size)
{
    DwcSsiState *s = DWC_SSI(opaque);

    if (dwc_ssi_is_dr(addr)) {
        dwc_ssi_push_tx(s, value);
        return;
    }

    if (dwc_ssi_is_unsupported_offset(addr)) {
        dwc_ssi_unsupported_write(s, addr, value);
        return;
    }

    if (dwc_ssi_write_requires_disabled(addr) &&
        dwc_ssi_is_enabled(s)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to offset 0x%" HWADDR_PRIx
                      " while SSI is enabled\n",
                      DEVICE(s)->canonical_path, addr);
        return;
    }

    switch (addr) {
    case A_CTRLR0: {
        uint32_t dfs = FIELD_EX32(value, CTRLR0, DFS);

        if (dfs < 3) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unsupported DFS value %u (must encode "
                          "4..32 bits)\n",
                          DEVICE(s)->canonical_path, dfs);
            return;
        }

        if (FIELD_EX32(value, CTRLR0, SPI_FRF) != 0) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: enhanced SPI frame formats are not modelled\n",
                          DEVICE(s)->canonical_path);
        }
        dwc_ssi_write_masked(s, R_CTRLR0, value,
                             DWC_SSI_CTRLR0_STANDARD_WRITABLE_MASK);
        break;
    }
    case A_CTRLR1:
        dwc_ssi_write_masked(s, R_CTRLR1, value,
                             DWC_SSI_CTRLR1_WRITABLE_MASK);
        break;
    case A_SSIENR: {
        bool old_enabled = dwc_ssi_is_enabled(s);
        bool new_enabled = value & R_SSIENR_SSIC_EN_MASK;

        if (old_enabled == new_enabled) {
            return;
        }

        s->regs[R_SSIENR] = FIELD_DP32(0, SSIENR, SSIC_EN, new_enabled);
        if (!new_enabled) {
            dwc_ssi_abort_transfer(s);
            return;
        }

        dwc_ssi_update_cs(s);
        dwc_ssi_run_transfer(s);
        dwc_ssi_update_irq(s);
        break;
    }
    case A_MWCR:
        dwc_ssi_write_masked(s, R_MWCR, value,
                             DWC_SSI_MWCR_WRITABLE_MASK);
        break;
    case A_SER: {
        uint32_t old_ser = s->regs[R_SER];
        uint32_t new_ser = value & MAKE_64BIT_MASK(0, s->cfg.num_cs);

        /*
         * Switching CS aborts pending xfer.
         * Stale RO/EEPROM phase must not resume on the new slave.
         */
        if (old_ser != new_ser && s->phase != DWC_SSI_PHASE_IDLE) {
            s->phase = DWC_SSI_PHASE_IDLE;
            s->remaining_frames = 0;
        }
        s->regs[R_SER] = new_ser;

        dwc_ssi_update_cs(s);
        dwc_ssi_run_transfer(s);
        dwc_ssi_update_irq(s);
        break;
    }
    case A_BAUDR:
        dwc_ssi_write_masked(s, R_BAUDR, value,
                             DWC_SSI_BAUDR_WRITABLE_MASK);
        break;
    case A_TXFTLR:
        if (!dwc_ssi_fifo_threshold_valid(
                s, FIELD_EX32(value, TXFTLR, TFT)) ||
            !dwc_ssi_fifo_threshold_valid(
                s, FIELD_EX32(value, TXFTLR, TXFTHR))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid txftlr value %x\n",
                          DEVICE(s)->canonical_path, (unsigned int)value);
            break;
        }
        dwc_ssi_write_masked(s, R_TXFTLR, value,
                             DWC_SSI_TXFTLR_WRITABLE_MASK);
        dwc_ssi_run_transfer(s);
        dwc_ssi_update_irq(s);
        break;
    case A_RXFTLR:
        if (!dwc_ssi_fifo_threshold_valid(
                s, FIELD_EX32(value, RXFTLR, RFT))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid rxftlr value %x\n",
                          DEVICE(s)->canonical_path, (unsigned int)value);
            break;
        }
        dwc_ssi_write_masked(s, R_RXFTLR, value,
                             DWC_SSI_RXFTLR_WRITABLE_MASK);
        dwc_ssi_update_irq(s);
        break;
    case A_TXFLR:
    case A_RXFLR:
    case A_SR:
        break;
    case A_IMR:
        dwc_ssi_write_masked(s, R_IMR, value,
                             DWC_SSI_IMR_WRITABLE_MASK);
        dwc_ssi_update_irq(s);
        break;
    case A_ISR:
    case A_RISR:
        break;
    case A_TXEICR:
    case A_RXOICR:
    case A_RXUICR:
    case A_MSTICR:
    case A_ICR:
        break;
    case A_IDR:
    case A_SSIC_VERSION_ID:
        break;
    default:
        if (addr >= DWC_SSI_REGS_SIZE || (addr & 0x3) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                          DEVICE(s)->canonical_path, addr);
        }
        break;
    }
}

static const MemoryRegionOps dwc_ssi_ops = {
    .read = dwc_ssi_read,
    .write = dwc_ssi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void dwc_ssi_enter_reset(Object *obj, ResetType type)
{
    DwcSsiState *s = DWC_SSI(obj);

    memset(s->regs, 0, sizeof(s->regs));
    fifo32_reset(&s->tx_fifo);
    fifo32_reset(&s->rx_fifo);
    s->phase = DWC_SSI_PHASE_IDLE;
    s->remaining_frames = 0;
    s->dummy_frame = 0;
    s->irq_latched = 0;

    s->regs[R_CTRLR0] = DWC_SSI_CTRLR0_RESET;
    s->regs[R_SR] = DWC_SSI_SR_RESET;
    s->regs[R_IMR] = dwc_ssi_imr_reset(s);
    s->regs[R_IDR] = DWC_SSI_IDR_RESET;
    s->regs[R_SSIC_VERSION_ID] = DWC_SSI_VERSION;

    dwc_ssi_update_irq(s);
}

static void dwc_ssi_hold_reset(Object *obj, ResetType type)
{
    DwcSsiState *s = DWC_SSI(obj);
    s->active_cs = -1;

    if (s->cs_lines) {
        for (int i = 0; i < s->cfg.num_cs; i++) {
            qemu_irq_raise(s->cs_lines[i]);
        }
    }
}

static void dwc_ssi_exit_reset(Object *obj, ResetType type)
{
    DwcSsiState *s = DWC_SSI(obj);

    dwc_ssi_update_irq(s);
}

static bool dwc_ssi_fifo_valid(const Fifo32 *fifo, uint32_t depth)
{
    const Fifo8 *fifo8 = &fifo->fifo;
    uint32_t capacity = depth * sizeof(uint32_t);

    return fifo8->capacity == capacity &&
           (fifo8->capacity % sizeof(uint32_t)) == 0 &&
           fifo8->head < fifo8->capacity &&
           fifo8->num <= fifo8->capacity &&
           (fifo8->head % sizeof(uint32_t)) == 0 &&
           (fifo8->num % sizeof(uint32_t)) == 0;
}

static int dwc_ssi_post_load(void *opaque, int version_id)
{
    DwcSsiState *s = opaque;
    uint32_t ndf = FIELD_EX32(s->regs[R_CTRLR1], CTRLR1, NDF) + 1;

    if (s->active_cs < -1 || s->active_cs >= (int)s->cfg.num_cs) {
        return -EINVAL;
    }
    if (s->irq_latched & ~DWC_SSI_IMPLEMENTED_LATCHED_IRQ_MASK) {
        return -EINVAL;
    }
    if (!dwc_ssi_fifo_valid(&s->tx_fifo, s->cfg.fifo_depth) ||
        !dwc_ssi_fifo_valid(&s->rx_fifo, s->cfg.fifo_depth)) {
        return -EINVAL;
    }
    if (s->phase >= DWC_SSI_PHASE_COUNT) {
        return -EINVAL;
    }
    if (s->phase == DWC_SSI_PHASE_RX_ONLY ||
        s->phase == DWC_SSI_PHASE_EEPROM_DATA) {
        if (s->remaining_frames == 0 || s->remaining_frames > ndf) {
            return -EINVAL;
        }
    } else if (s->remaining_frames != 0) {
        return -EINVAL;
    }
    if (s->active_cs >= 0 &&
        (!dwc_ssi_is_enabled(s) ||
         s->regs[R_SER] != BIT(s->active_cs))) {
        return -EINVAL;
    }

    for (int i = 0; i < s->cfg.num_cs; i++) {
        qemu_irq_raise(s->cs_lines[i]);
    }
    if (s->active_cs >= 0) {
        qemu_irq_lower(s->cs_lines[s->active_cs]);
    }

    dwc_ssi_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_dwc_ssi = {
    .name = TYPE_DWC_SSI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dwc_ssi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(cfg.num_cs, DwcSsiState),
        VMSTATE_UINT32_EQUAL(cfg.fifo_depth, DwcSsiState),
        VMSTATE_BOOL(cfg.master_mode, DwcSsiState),
        VMSTATE_UINT32_ARRAY(regs, DwcSsiState, DWC_SSI_NUM_REGS),
        VMSTATE_FIFO32(tx_fifo, DwcSsiState),
        VMSTATE_FIFO32(rx_fifo, DwcSsiState),
        VMSTATE_UINT32(irq_latched, DwcSsiState),
        VMSTATE_UINT32(phase, DwcSsiState),
        VMSTATE_UINT32(remaining_frames, DwcSsiState),
        VMSTATE_UINT32(dummy_frame, DwcSsiState),
        VMSTATE_INT32(active_cs, DwcSsiState),
        VMSTATE_END_OF_LIST()
    },
};

static void dwc_ssi_init(Object *obj)
{
    DwcSsiState *s = DWC_SSI(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->spi = ssi_create_bus(dev, "spi");

    memory_region_init_io(&s->mmio, obj, &dwc_ssi_ops, s,
                          TYPE_DWC_SSI, DWC_SSI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    for (int i = 0; i < DWC_SSI_IRQ_COUNT; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }

    s->active_cs = -1;
}

static void dwc_ssi_realize(DeviceState *dev, Error **errp)
{
    DwcSsiState *s = DWC_SSI(dev);

    if (!dwc_ssi_validate_config(s, errp)) {
        return;
    }

    s->cs_lines = g_new0(qemu_irq, s->cfg.num_cs);
    qdev_init_gpio_out_named(dev, s->cs_lines, "cs", s->cfg.num_cs);

    fifo32_create(&s->tx_fifo, s->cfg.fifo_depth);
    fifo32_create(&s->rx_fifo, s->cfg.fifo_depth);
}

static void dwc_ssi_finalize(Object *obj)
{
    DwcSsiState *s = DWC_SSI(obj);

    fifo32_destroy(&s->tx_fifo);
    fifo32_destroy(&s->rx_fifo);
    g_free(s->cs_lines);
}

static const Property dwc_ssi_properties[] = {
    DEFINE_PROP_UINT32("num-cs", DwcSsiState, cfg.num_cs, 1),
    DEFINE_PROP_UINT32("fifo-depth", DwcSsiState, cfg.fifo_depth, 256),
    DEFINE_PROP_BOOL("master-mode", DwcSsiState, cfg.master_mode, true),
};

static void dwc_ssi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = dwc_ssi_realize;
    dc->vmsd = &vmstate_dwc_ssi;
    device_class_set_props(dc, dwc_ssi_properties);
    rc->phases.enter = dwc_ssi_enter_reset;
    rc->phases.hold = dwc_ssi_hold_reset;
    rc->phases.exit = dwc_ssi_exit_reset;
}

static const TypeInfo dwc_ssi_info = {
    .name = TYPE_DWC_SSI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DwcSsiState),
    .instance_init = dwc_ssi_init,
    .instance_finalize = dwc_ssi_finalize,
    .class_init = dwc_ssi_class_init,
};

static void dwc_ssi_register_types(void)
{
    type_register_static(&dwc_ssi_info);
}

type_init(dwc_ssi_register_types)
