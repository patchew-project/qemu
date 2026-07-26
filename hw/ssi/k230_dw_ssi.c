/*
 * Kendryte K230 DesignWare SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Emulates the DesignWare SSI controllers documented by the K230
 * Technical Reference Manual, including standard SPI, Dual/Quad SDR,
 * internal DMA, and the XIP read window.
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */

#include "qemu/osdep.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/ssi/k230_dw_ssi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/dma.h"

#define K230_DW_SSI_FIFO_CAPACITY 256

#define K230_DW_SSI_CTRLR0_RESET            0x00004007
#define K230_DW_SSI_SR_RESET                0x00000006
#define K230_DW_SSI_IMR_RESET               0x0000003f
#define K230_DW_SSI_IDR_RESET               0xa1b2c3d5
#define K230_DW_SSI_SPI_CTRLR0_SPI_RESET    0x04000200
#define K230_DW_SSI_SPI_CTRLR0_FMC_RESET    0x28000200
#define K230_DW_SSI_AXIAWLEN_RESET          0x00000700
#define K230_DW_SSI_AXIARLEN_RESET          0x00000700
#define K230_DW_SSI_VERSION                 0x3130332a
#define K230_DW_SSI_PIO_TX_BATCH            64
#define K230_DW_SSI_IRQ_VALID_MASK          0x000009bf

enum {
    K230_DW_SSI_TMOD_TR,
    K230_DW_SSI_TMOD_TO,
    K230_DW_SSI_TMOD_RO,
    K230_DW_SSI_TMOD_EEPROM_READ,
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

#define K230_DW_SSI_CTRLR0_WRITABLE_MASK \
    (R_CTRLR0_DFS_MASK | \
     R_CTRLR0_SCPH_MASK | \
     R_CTRLR0_SCPOL_MASK | \
     R_CTRLR0_TMOD_MASK | \
     R_CTRLR0_SLV_OE_MASK | \
     R_CTRLR0_SRL_MASK | \
     R_CTRLR0_SSTE_MASK | \
     R_CTRLR0_CFS_MASK | \
     R_CTRLR0_SPI_FRF_MASK | \
     R_CTRLR0_SPI_HYPERBUS_EN_MASK)

#define K230_DW_SSI_CTRLR1_WRITABLE_MASK R_CTRLR1_NDF_MASK
#define K230_DW_SSI_MWCR_WRITABLE_MASK \
    (R_MWCR_MWMOD_MASK | R_MWCR_MDD_MASK)
#define K230_DW_SSI_BAUDR_WRITABLE_MASK R_BAUDR_SCKDV_MASK
#define K230_DW_SSI_TXFTLR_WRITABLE_MASK \
    (R_TXFTLR_TFT_MASK | R_TXFTLR_TXFTHR_MASK)
#define K230_DW_SSI_RXFTLR_WRITABLE_MASK R_RXFTLR_RFT_MASK
#define K230_DW_SSI_DMACR_WRITABLE_MASK \
    (R_DMACR_IDMAE_MASK | R_DMACR_ATW_MASK | R_DMACR_AINC_MASK | \
     R_DMACR_ACACHE_MASK | R_DMACR_APROT_MASK | R_DMACR_AID_MASK)
#define K230_DW_SSI_AXIAWLEN_WRITABLE_MASK R_AXIAWLEN_AWLEN_MASK
#define K230_DW_SSI_AXIARLEN_WRITABLE_MASK R_AXIARLEN_ARLEN_MASK
#define K230_DW_SSI_IMR_WRITABLE_MASK \
    (R_IMR_TXEIM_MASK | R_IMR_TXOIM_MASK | R_IMR_RXUIM_MASK | \
     R_IMR_RXOIM_MASK | R_IMR_RXFIM_MASK | R_IMR_MSTIM_MASK | \
     R_IMR_TXUIM_MASK | R_IMR_AXIEM_MASK | R_IMR_DONEM_MASK)
#define K230_DW_SSI_RX_SAMPLE_DELAY_WRITABLE_MASK \
    (R_RX_SAMPLE_DELAY_RSD_MASK | R_RX_SAMPLE_DELAY_SE_MASK)
#define K230_DW_SSI_SPI_CTRLR0_WRITABLE_MASK \
    (R_SPI_CTRLR0_CLK_STRETCH_EN_MASK | \
     R_SPI_CTRLR0_XIP_PREFETCH_EN_MASK | \
     R_SPI_CTRLR0_XIP_MBL_MASK | \
     R_SPI_CTRLR0_SPI_RXDS_SIG_EN_MASK | \
     R_SPI_CTRLR0_SPI_DM_EN_MASK | \
     R_SPI_CTRLR0_SSIC_XIP_CONT_XFER_EN_MASK | \
     R_SPI_CTRLR0_XIP_INST_EN_MASK | \
     R_SPI_CTRLR0_XIP_DFS_HC_MASK | \
     R_SPI_CTRLR0_INST_DDR_EN_MASK | \
     R_SPI_CTRLR0_SPI_DDR_EN_MASK | \
     R_SPI_CTRLR0_SPI_RXDS_EN_MASK | \
     R_SPI_CTRLR0_WAIT_CYCLES_MASK | \
     R_SPI_CTRLR0_INST_L_MASK | \
     R_SPI_CTRLR0_XIP_MD_BIT_EN_MASK | \
     R_SPI_CTRLR0_ADDR_L_MASK | \
     R_SPI_CTRLR0_TRANS_TYPE_MASK)
#define K230_DW_SSI_DDR_DRIVE_EDGE_WRITABLE_MASK \
    R_DDR_DRIVE_EDGE_TDE_MASK
#define K230_DW_SSI_XIP_MODE_BITS_WRITABLE_MASK \
    R_XIP_MODE_BITS_XIP_MD_BITS_MASK
#define K230_DW_SSI_XIP_INCR_INST_WRITABLE_MASK \
    R_XIP_INCR_INST_INCR_INST_MASK
#define K230_DW_SSI_XIP_WRAP_INST_WRITABLE_MASK \
    R_XIP_WRAP_INST_WRAP_INST_MASK
#define K230_DW_SSI_SPIDR_WRITABLE_MASK R_SPIDR_SPI_INST_MASK
#define K230_DW_SSI_SPIAR_WRITABLE_MASK R_SPIAR_SDAR_MASK
#define K230_DW_SSI_AXIAR0_WRITABLE_MASK R_AXIAR0_AXIAR_0_31_MASK
#define K230_DW_SSI_AXIAR1_WRITABLE_MASK R_AXIAR1_AXIAR_32_63_MASK

static const uint32_t k230_dw_ssi_irq_status_mask[
    K230_DW_SSI_IRQ_COUNT] = {
    [K230_DW_SSI_IRQ_TXE] = R_RISR_TXEIR_MASK,
    [K230_DW_SSI_IRQ_TXO] = R_RISR_TXOIR_MASK,
    [K230_DW_SSI_IRQ_RXF] = R_RISR_RXFIR_MASK,
    [K230_DW_SSI_IRQ_RXO] = R_RISR_RXOIR_MASK,
    [K230_DW_SSI_IRQ_TXU] = R_RISR_TXUIR_MASK,
    [K230_DW_SSI_IRQ_RXU] = R_RISR_RXUIR_MASK,
    [K230_DW_SSI_IRQ_MST] = R_RISR_MSTIR_MASK,
    [K230_DW_SSI_IRQ_DONE] = R_RISR_DONER_MASK,
    [K230_DW_SSI_IRQ_AXIE] = R_RISR_AXIER_MASK,
};

static void k230_dw_ssi_write_masked(K230DwSsiState *s, unsigned int reg,
                                     uint32_t value, uint32_t mask)
{
    s->regs[reg] = (s->regs[reg] & ~mask) | (value & mask);
}

static uint32_t k230_dw_ssi_irq_raw_status(K230DwSsiState *s)
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
    return status & K230_DW_SSI_IRQ_VALID_MASK;
}

static void k230_dw_ssi_update_irq(K230DwSsiState *s)
{
    uint32_t status = k230_dw_ssi_irq_raw_status(s) &
                      s->regs[R_IMR] & K230_DW_SSI_IRQ_VALID_MASK;

    for (int i = 0; i < K230_DW_SSI_IRQ_COUNT; i++) {
        qemu_set_irq(s->irqs[i], !!(status & k230_dw_ssi_irq_status_mask[i]));
    }
}

static uint32_t k230_dw_ssi_irq_read_clear(K230DwSsiState *s,
                                            uint32_t clear_mask)
{
    uint32_t active = s->irq_latched & clear_mask;

    s->irq_latched &= ~clear_mask;
    k230_dw_ssi_update_irq(s);
    return !!active;
}

static uint32_t k230_dw_ssi_frame_masked(K230DwSsiState *s)
{
    unsigned int bits = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, DFS) + 1;

    return bits == 32 ? UINT32_MAX : MAKE_64BIT_MASK(0, bits);
}

static bool k230_dw_ssi_enabled(const K230DwSsiState *s)
{
    return FIELD_EX32(s->regs[R_SSIENR], SSIENR, SSIC_EN);
}

static void k230_dw_ssi_deselect(K230DwSsiState *s)
{
    if (s->active_cs < 0) {
        return;
    }

    qemu_irq_raise(s->cs_lines[s->active_cs]);
    s->active_cs = -1;
}

static void k230_dw_ssi_select(K230DwSsiState *s, unsigned cs)
{
    if (cs >= s->num_cs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid chip select %u\n",
                      DEVICE(s)->canonical_path, cs);
        k230_dw_ssi_deselect(s);
        return;
    }

    if (s->active_cs == cs) {
        return;
    }

    k230_dw_ssi_deselect(s);
    qemu_irq_lower(s->cs_lines[cs]);
    s->active_cs = cs;
}

static void k230_dw_ssi_update_cs(K230DwSsiState *s)
{
    uint32_t ser = s->regs[R_SER];

    if (!k230_dw_ssi_enabled(s) || !ser) {
        k230_dw_ssi_deselect(s);
        return;
    }

    if (ser & (ser - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: multiple chip selects enabled: 0x%x\n",
                      DEVICE(s)->canonical_path, ser);
        k230_dw_ssi_deselect(s);
        return;
    }

    k230_dw_ssi_select(s, ctz32(ser));
}

static void k230_dw_ssi_abort_transfer(K230DwSsiState *s)
{
    k230_dw_ssi_deselect(s);
    fifo32_reset(&s->tx_fifo);
    fifo32_reset(&s->rx_fifo);
    s->phase = K230_DW_SSI_PHASE_IDLE;
    s->remaining_frames = 0;
    memset(&s->enhanced, 0, sizeof(s->enhanced));
    k230_dw_ssi_update_irq(s);
}

static uint32_t k230_dw_ssi_status(K230DwSsiState *s)
{
    uint32_t tx_used = fifo32_num_used(&s->tx_fifo);
    uint32_t rx_used = fifo32_num_used(&s->rx_fifo);
    uint32_t sr = 0;

    sr = FIELD_DP32(sr, SR, BUSY, s->phase != K230_DW_SSI_PHASE_IDLE);
    sr = FIELD_DP32(sr, SR, TFNF, tx_used < K230_DW_SSI_FIFO_CAPACITY);
    sr = FIELD_DP32(sr, SR, TFE, tx_used == 0);
    sr = FIELD_DP32(sr, SR, RFNE, rx_used != 0);
    sr = FIELD_DP32(sr, SR, RFF,
                    rx_used == K230_DW_SSI_FIFO_CAPACITY);
    sr = FIELD_DP32(sr, SR, CMPLTD_DF, s->idma_completed_frames);

    return sr;
}

static void k230_dw_ssi_run_transfer(K230DwSsiState *s);

static void k230_dw_ssi_push_tx(K230DwSsiState *s, uint32_t tx)
{
    if (!k230_dw_ssi_enabled(s)) {
        return;
    }

    if (fifo32_is_full(&s->tx_fifo)) {
        s->irq_latched |= R_RISR_TXOIR_MASK;
        k230_dw_ssi_update_irq(s);
        return;
    }

    fifo32_push(&s->tx_fifo, tx);

    if (s->phase != K230_DW_SSI_PHASE_STANDARD_TX_ONLY) {
        k230_dw_ssi_run_transfer(s);
    }
    k230_dw_ssi_update_irq(s);
}

static uint32_t k230_dw_ssi_send_frame(K230DwSsiState *s,
                                        uint32_t tx)
{
    uint32_t mask = k230_dw_ssi_frame_masked(s);
    uint32_t rx;

    tx &= mask;

    if (FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, SRL)) {
        rx = tx;
    } else {
        rx = ssi_transfer(s->spi, tx);
    }

    return rx & mask;
}

static bool k230_dw_ssi_enhanced_config_supported(K230DwSsiState *s)
{
    uint32_t ctrlr0 = s->regs[R_CTRLR0];
    uint32_t spi_ctrlr0 = s->regs[R_SPI_CTRLR0];
    uint32_t spi_frf;
    uint32_t trans_type;
    uint32_t tmod;
    uint32_t required_lines;

    spi_frf = FIELD_EX32(ctrlr0, CTRLR0, SPI_FRF);
    trans_type = FIELD_EX32(spi_ctrlr0, SPI_CTRLR0, TRANS_TYPE);
    tmod = FIELD_EX32(ctrlr0, CTRLR0, TMOD);

    switch (spi_frf) {
    case 1: /* Dual */
        required_lines = 2;
        break;
    case 2: /* Quad */
        required_lines = 4;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported SPI_FRF=%u\n",
                      DEVICE(s)->canonical_path, spi_frf);
        return false;
    }

    if (required_lines > s->max_lines) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SPI_FRF=%u requires %u lines, only %u available\n",
                      DEVICE(s)->canonical_path,
                      spi_frf, required_lines, s->max_lines);
        return false;
    }

    if (trans_type > 2) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported TRANS_TYPE=%u\n",
                      DEVICE(s)->canonical_path, trans_type);
        return false;
    }

    if (tmod != K230_DW_SSI_TMOD_RO && tmod != K230_DW_SSI_TMOD_TO) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported enhanced TMOD=%u\n",
                      DEVICE(s)->canonical_path, tmod);
        return false;
    }

    if (FIELD_EX32(spi_ctrlr0, SPI_CTRLR0, SPI_DDR_EN) ||
        FIELD_EX32(spi_ctrlr0, SPI_CTRLR0, INST_DDR_EN) ||
        FIELD_EX32(spi_ctrlr0, SPI_CTRLR0, SPI_RXDS_EN) ||
        FIELD_EX32(spi_ctrlr0, SPI_CTRLR0, SPI_RXDS_SIG_EN)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: DDR/RXDS enhanced mode is unsupported\n",
                      DEVICE(s)->canonical_path);
        return false;
    }

    return true;
}

static bool k230_dw_ssi_decode_enhanced_command(
    K230DwSsiState *s, K230DwSsiEnhancedCommand *command)
{
    uint32_t spi_frf = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, SPI_FRF);
    uint32_t inst_l = FIELD_EX32(s->regs[R_SPI_CTRLR0], SPI_CTRLR0, INST_L);
    uint32_t addr_l = FIELD_EX32(s->regs[R_SPI_CTRLR0], SPI_CTRLR0, ADDR_L);
    uint32_t trans_type = FIELD_EX32(s->regs[R_SPI_CTRLR0],
                                     SPI_CTRLR0, TRANS_TYPE);
    uint32_t inst_bits;
    uint32_t addr_bits = addr_l << 2;
    uint32_t mode_bits = 0;
    bool mode_bits_enabled =
        FIELD_EX32(s->regs[R_SPI_CTRLR0], SPI_CTRLR0, XIP_MD_BIT_EN);

    if (!k230_dw_ssi_enhanced_config_supported(s)) {
        return false;
    }

    inst_bits = inst_l ? (1U << (inst_l + 1)) : 0;

    if (addr_bits > 32) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported enhanced address length %u bits\n",
                      DEVICE(s)->canonical_path, addr_bits);
        return false;
    }

    if (mode_bits_enabled) {
        uint32_t mode_length_encoding =
            FIELD_EX32(s->regs[R_SPI_CTRLR0], SPI_CTRLR0, XIP_MBL);

        mode_bits = 1U << (mode_length_encoding + 1);
    }

    command->instruction_bits = inst_bits;
    command->address_bits = addr_bits;
    command->mode_bits = mode_bits;
    command->mode_bits_enabled = mode_bits_enabled;
    command->wait_cycles =
        FIELD_EX32(s->regs[R_SPI_CTRLR0], SPI_CTRLR0, WAIT_CYCLES);
    command->data_frames =
        FIELD_EX32(s->regs[R_CTRLR1], CTRLR1, NDF) + 1;
    command->spi_frf = spi_frf;
    command->trans_type = trans_type;
    command->tmod = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, TMOD);

    if (mode_bits_enabled) {
        command->mode = s->regs[R_XIP_MODE_BITS] &
            (uint32_t)MAKE_64BIT_MASK(0, mode_bits);
    }

    return true;
}

static bool k230_dw_ssi_prepare_enhanced_command(K230DwSsiState *s)
{
    K230DwSsiEnhancedCommand command = { 0 };
    uint32_t required_items;

    if (!k230_dw_ssi_decode_enhanced_command(s, &command)) {
        return false;
    }

    required_items = (command.instruction_bits != 0) +
                     (command.address_bits != 0);
    if (fifo32_num_used(&s->tx_fifo) < required_items) {
        return false;
    }

    if (command.instruction_bits != 0) {
        command.instruction = fifo32_pop(&s->tx_fifo) &
            (uint32_t)MAKE_64BIT_MASK(0, command.instruction_bits);
    }
    if (command.address_bits != 0) {
        command.address = fifo32_pop(&s->tx_fifo) &
            (uint32_t)MAKE_64BIT_MASK(0, command.address_bits);
    }

    s->enhanced = command;
    s->remaining_frames = command.data_frames;
    s->phase = K230_DW_SSI_PHASE_ENHANCED_INSTRUCTION;
    return true;
}

static void k230_dw_ssi_send_enhanced_field(K230DwSsiState *s,
                                             uint32_t value,
                                             uint32_t bits)
{
    uint32_t bytes = DIV_ROUND_UP(bits, 8);

    for (uint32_t i = 0; i < bytes; i++) {
        uint32_t shift = (bytes - i - 1) * 8;

        ssi_transfer(s->spi, (value >> shift) & 0xff);
    }
}

static uint32_t k230_dw_ssi_dummy_bytes(uint32_t spi_frf,
                                         uint32_t trans_type,
                                         uint32_t wait_cycles)
{
    uint32_t lines = 1;

    if (trans_type != 0) {
        lines = spi_frf == 1 ? 2 : 4;
    }

    return DIV_ROUND_UP(wait_cycles * lines, 8);
}

static bool k230_dw_ssi_idma_enabled(const K230DwSsiState *s)
{
    return FIELD_EX32(s->regs[R_DMACR], DMACR, IDMAE);
}

static uint64_t k230_dw_ssi_idma_address(const K230DwSsiState *s)
{
    return s->regs[R_AXIAR0] | ((uint64_t)s->regs[R_AXIAR1] << 32);
}

static bool k230_dw_ssi_idma_triggered(const K230DwSsiState *s)
{
    uint32_t ser = s->regs[R_SER];

    return k230_dw_ssi_idma_enabled(s) &&
           k230_dw_ssi_enabled(s) && ser &&
           !(ser & (ser - 1)) &&
           s->phase == K230_DW_SSI_PHASE_IDLE;
}

static void k230_dw_ssi_idma_end(K230DwSsiState *s, uint32_t cause)
{
    s->regs[R_SSIENR] = 0;
    s->phase = K230_DW_SSI_PHASE_IDLE;
    s->remaining_frames = 0;
    k230_dw_ssi_deselect(s);
    s->irq_latched |= cause;
    k230_dw_ssi_update_irq(s);
}

static void k230_dw_ssi_idma_fail(K230DwSsiState *s, const char *operation)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: IDMA %s memory access failed\n",
                  DEVICE(s)->canonical_path, operation);
    s->idma_completed_frames = 0;
    k230_dw_ssi_idma_end(s, R_RISR_AXIER_MASK);
}

/*
 * Supported SDK paths observe the final memory contents and DONE/AXIE,
 * so complete IDMA synchronously without modeling AXI timing or FIFO
 * backpressure.
 */
static void k230_dw_ssi_try_idma(K230DwSsiState *s)
{
    K230DwSsiEnhancedCommand command = { 0 };
    g_autofree uint8_t *buffer = NULL;
    uint64_t address;
    uint32_t dummy_bytes;
    uint32_t length;
    MemTxResult result;

    if (!k230_dw_ssi_idma_triggered(s)) {
        return;
    }

    if (!FIELD_EX32(s->regs[R_DMACR], DMACR, AINC)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: fixed-address IDMA is unsupported\n",
                      DEVICE(s)->canonical_path);
        s->idma_completed_frames = 0;
        k230_dw_ssi_idma_end(s, 0);
        return;
    }

    if (!fifo32_is_empty(&s->tx_fifo) ||
        !fifo32_is_empty(&s->rx_fifo)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: IDMA requires empty TX and RX FIFOs\n",
                      DEVICE(s)->canonical_path);
        s->idma_completed_frames = 0;
        k230_dw_ssi_idma_end(s, 0);
        return;
    }

    if (FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, DFS) != 7) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: IDMA only supports 8-bit data frames\n",
                      DEVICE(s)->canonical_path);
        s->idma_completed_frames = 0;
        k230_dw_ssi_idma_end(s, 0);
        return;
    }

    if (!k230_dw_ssi_decode_enhanced_command(s, &command)) {
        s->idma_completed_frames = 0;
        k230_dw_ssi_idma_end(s, 0);
        return;
    }

    command.instruction = s->regs[R_SPIDR] &
        (uint32_t)MAKE_64BIT_MASK(0, command.instruction_bits);
    command.address = s->regs[R_SPIAR] &
        (uint32_t)MAKE_64BIT_MASK(0, command.address_bits);
    length = command.data_frames;
    address = k230_dw_ssi_idma_address(s);
    s->idma_completed_frames = 0;

    if (address > UINT64_MAX - (length - 1)) {
        k230_dw_ssi_idma_fail(s, "address range");
        return;
    }

    buffer = g_malloc(length);
    if (command.tmod == K230_DW_SSI_TMOD_TO) {
        result = dma_memory_read(&address_space_memory, address, buffer,
                                 length, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            k230_dw_ssi_idma_fail(s, "source");
            return;
        }
    }

    k230_dw_ssi_update_cs(s);
    if (s->active_cs < 0) {
        k230_dw_ssi_idma_end(s, 0);
        return;
    }

    if (command.instruction_bits != 0) {
        k230_dw_ssi_send_enhanced_field(s, command.instruction,
                                         command.instruction_bits);
    }
    if (command.address_bits != 0) {
        k230_dw_ssi_send_enhanced_field(s, command.address,
                                         command.address_bits);
    }
    if (command.mode_bits_enabled) {
        k230_dw_ssi_send_enhanced_field(s, command.mode,
                                         command.mode_bits);
    } else if (command.trans_type == 1 && command.wait_cycles >= 2) {
        /*
         * The SDK's 1-4-4 read supplies its mode byte through XIP_MODE_BITS
         * without XIP_MD_BIT_EN; one Quad byte consumes two wait cycles.
         */
        k230_dw_ssi_send_enhanced_field(s, s->regs[R_XIP_MODE_BITS], 8);
        command.wait_cycles -= 2;
    }

    dummy_bytes = k230_dw_ssi_dummy_bytes(
        command.spi_frf, command.trans_type, command.wait_cycles);
    for (uint32_t i = 0; i < dummy_bytes; i++) {
        ssi_transfer(s->spi, 0);
    }

    if (command.tmod == K230_DW_SSI_TMOD_RO) {
        for (uint32_t i = 0; i < length; i++) {
            buffer[i] = ssi_transfer(s->spi, 0);
        }
        result = dma_memory_write(&address_space_memory, address, buffer,
                                  length, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            k230_dw_ssi_idma_fail(s, "destination");
            return;
        }
    } else {
        for (uint32_t i = 0; i < length; i++) {
            ssi_transfer(s->spi, buffer[i]);
        }
    }

    s->idma_completed_frames = length;
    k230_dw_ssi_idma_end(s, R_RISR_DONER_MASK);
}

static void k230_dw_ssi_run_enhanced_rx_data(K230DwSsiState *s)
{
    while (!fifo32_is_full(&s->rx_fifo) &&
           s->remaining_frames > 0) {
        uint32_t rx = ssi_transfer(s->spi, 0);

        fifo32_push(&s->rx_fifo,
                    rx & k230_dw_ssi_frame_masked(s));
        s->remaining_frames--;
    }
}

static void k230_dw_ssi_run_enhanced_tx_data(K230DwSsiState *s)
{
    uint32_t mask = k230_dw_ssi_frame_masked(s);

    while (!fifo32_is_empty(&s->tx_fifo) &&
           s->remaining_frames > 0) {
        uint32_t tx = fifo32_pop(&s->tx_fifo);

        ssi_transfer(s->spi, tx & mask);
        s->remaining_frames--;
    }
}

static void k230_dw_ssi_run_enhanced_transfer(K230DwSsiState *s)
{
    if (s->phase == K230_DW_SSI_PHASE_IDLE) {
        if (!k230_dw_ssi_prepare_enhanced_command(s)) {
            return;
        }
    }

    if (s->phase == K230_DW_SSI_PHASE_ENHANCED_INSTRUCTION) {
        if (s->enhanced.instruction_bits != 0) {
            k230_dw_ssi_send_enhanced_field(
                s, s->enhanced.instruction,
                s->enhanced.instruction_bits);
        }
        s->phase = K230_DW_SSI_PHASE_ENHANCED_ADDRESS;
    }

    if (s->phase == K230_DW_SSI_PHASE_ENHANCED_ADDRESS) {
        if (s->enhanced.address_bits != 0) {
            k230_dw_ssi_send_enhanced_field(
                s, s->enhanced.address, s->enhanced.address_bits);
        }
        s->phase = K230_DW_SSI_PHASE_ENHANCED_MODE;
    }

    if (s->phase == K230_DW_SSI_PHASE_ENHANCED_MODE) {
        if (s->enhanced.mode_bits_enabled) {
            k230_dw_ssi_send_enhanced_field(
                s, s->enhanced.mode, s->enhanced.mode_bits);
        }
        s->phase = K230_DW_SSI_PHASE_ENHANCED_DUMMY;
    }

    if (s->phase == K230_DW_SSI_PHASE_ENHANCED_DUMMY) {
        uint32_t dummy_bytes = k230_dw_ssi_dummy_bytes(
            s->enhanced.spi_frf, s->enhanced.trans_type,
            s->enhanced.wait_cycles);

        for (uint32_t i = 0; i < dummy_bytes; i++) {
            ssi_transfer(s->spi, 0);
        }
        s->phase = K230_DW_SSI_PHASE_ENHANCED_DATA;
    }

    if (s->phase != K230_DW_SSI_PHASE_ENHANCED_DATA) {
        g_assert_not_reached();
    }

    switch (s->enhanced.tmod) {
    case K230_DW_SSI_TMOD_RO:
        k230_dw_ssi_run_enhanced_rx_data(s);
        break;
    case K230_DW_SSI_TMOD_TO:
        k230_dw_ssi_run_enhanced_tx_data(s);
        break;
    default:
        g_assert_not_reached();
    }

    if (s->remaining_frames == 0) {
        s->phase = K230_DW_SSI_PHASE_IDLE;
    }
}

static void k230_dw_ssi_run_transfer(K230DwSsiState *s)
{
    uint32_t spi_frf;
    uint32_t tmod;

    if (!k230_dw_ssi_enabled(s) || s->active_cs < 0) {
        return;
    }

    spi_frf = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, SPI_FRF);
    if (spi_frf != 0) {
        k230_dw_ssi_run_enhanced_transfer(s);
        return;
    }

    tmod = FIELD_EX32(s->regs[R_CTRLR0], CTRLR0, TMOD);

    switch (tmod) {
    case K230_DW_SSI_TMOD_TR:
        while (!fifo32_is_empty(&s->tx_fifo)) {
            uint32_t tx = fifo32_pop(&s->tx_fifo);
            uint32_t rx = k230_dw_ssi_send_frame(s, tx);

            if (!fifo32_is_full(&s->rx_fifo)) {
                fifo32_push(&s->rx_fifo, rx);
            } else {
                s->irq_latched |= R_RISR_RXOIR_MASK;
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: RX FIFO full, dropping frame\n",
                              DEVICE(s)->canonical_path);
                break;
            }
        }
        break;
    case K230_DW_SSI_TMOD_TO: {
        unsigned int frames = 0;

        if (fifo32_is_empty(&s->tx_fifo)) {
            s->phase = K230_DW_SSI_PHASE_IDLE;
            break;
        }

        s->phase = K230_DW_SSI_PHASE_STANDARD_TX_ONLY;
        while (!fifo32_is_empty(&s->tx_fifo) &&
               frames < K230_DW_SSI_PIO_TX_BATCH) {
            uint32_t tx = fifo32_pop(&s->tx_fifo);

            k230_dw_ssi_send_frame(s, tx);
            frames++;
        }
        if (fifo32_is_empty(&s->tx_fifo)) {
            s->phase = K230_DW_SSI_PHASE_IDLE;
        }
        break;
    }
    case K230_DW_SSI_TMOD_RO:
        switch (s->phase) {
        case K230_DW_SSI_PHASE_IDLE:
            if (fifo32_is_empty(&s->tx_fifo)) {
                break;
            }
            fifo32_pop(&s->tx_fifo);
            s->phase = K230_DW_SSI_PHASE_RX_ONLY;
            s->remaining_frames = FIELD_EX32(s->regs[R_CTRLR1], CTRLR1,
                                              NDF) + 1;
            /* Fall through. */
        case K230_DW_SSI_PHASE_RX_ONLY:
            while (!fifo32_is_full(&s->rx_fifo) &&
                   s->remaining_frames > 0) {
                uint32_t rx = k230_dw_ssi_send_frame(s, 0x00);

                fifo32_push(&s->rx_fifo, rx);
                s->remaining_frames--;
            }
            if (s->remaining_frames == 0) {
                s->phase = K230_DW_SSI_PHASE_IDLE;
            }
            break;
        }
        break;
    case K230_DW_SSI_TMOD_EEPROM_READ:
        switch (s->phase) {
        case K230_DW_SSI_PHASE_IDLE:
            if (fifo32_is_empty(&s->tx_fifo)) {
                break;
            }
            s->phase = K230_DW_SSI_PHASE_EEPROM_COMMAND;
            /* Fall through. */
        case K230_DW_SSI_PHASE_EEPROM_COMMAND:
            if (fifo32_is_empty(&s->tx_fifo)) {
                break;
            }
            while (!fifo32_is_empty(&s->tx_fifo)) {
                uint32_t tx = fifo32_pop(&s->tx_fifo);

                k230_dw_ssi_send_frame(s, tx);
            }
            s->phase = K230_DW_SSI_PHASE_EEPROM_DATA;
            s->remaining_frames = FIELD_EX32(s->regs[R_CTRLR1], CTRLR1,
                                              NDF) + 1;
            /* Fall through. */
        case K230_DW_SSI_PHASE_EEPROM_DATA:
            while (!fifo32_is_full(&s->rx_fifo) &&
                   s->remaining_frames > 0) {
                uint32_t rx = k230_dw_ssi_send_frame(s, 0x00);

                fifo32_push(&s->rx_fifo, rx);
                s->remaining_frames--;
            }
            if (s->remaining_frames == 0) {
                s->phase = K230_DW_SSI_PHASE_IDLE;
            }
            break;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static bool k230_dw_ssi_is_dr(hwaddr addr)
{
    return addr >= A_DR0 && addr <= A_DR_END &&
           (addr & 0x3) == 0;
}

static bool k230_dw_ssi_is_razwi(hwaddr addr)
{
    switch (addr) {
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
        return true;
    default:
        return false;
    }
}

static bool k230_dw_ssi_write_requires_disabled(hwaddr addr)
{
    switch (addr) {
    case A_CTRLR0:
    case A_CTRLR1:
    case A_MWCR:
    case A_BAUDR:
    case A_SPI_CTRLR0:
        return true;
    default:
        return false;
    }
}

static uint64_t k230_dw_ssi_read(void *opaque, hwaddr addr, unsigned int size)
{
    K230DwSsiState *s = K230_DW_SSI(opaque);
    uint32_t value = 0;

    if (k230_dw_ssi_is_dr(addr)) {
        if (k230_dw_ssi_idma_enabled(s)) {
            return 0;
        }

        if (!fifo32_is_empty(&s->rx_fifo)) {
            value = fifo32_pop(&s->rx_fifo) & k230_dw_ssi_frame_masked(s);
        } else {
            value = 0;
            s->irq_latched |= R_RISR_RXUIR_MASK;
        }

        k230_dw_ssi_run_transfer(s);
        k230_dw_ssi_update_irq(s);
        return value;
    }

    if (k230_dw_ssi_is_razwi(addr)) {
        return 0;
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
    case A_DMACR:
    case A_AXIAWLEN:
    case A_AXIARLEN:
    case A_IDR:
    case A_SSIC_VERSION_ID:
    case A_RX_SAMPLE_DELAY:
    case A_SPI_CTRLR0:
    case A_DDR_DRIVE_EDGE:
    case A_XIP_MODE_BITS:
    case A_XIP_INCR_INST:
    case A_XIP_WRAP_INST:
    case A_SPIDR:
    case A_SPIAR:
    case A_AXIAR0:
    case A_AXIAR1:
        value = s->regs[addr / sizeof(uint32_t)];
        break;
    case A_SER:
        value = s->regs[R_SER] & MAKE_64BIT_MASK(0, s->num_cs);
        break;
    case A_TXFLR:
        value = fifo32_num_used(&s->tx_fifo);
        if (s->phase == K230_DW_SSI_PHASE_STANDARD_TX_ONLY) {
            k230_dw_ssi_run_transfer(s);
            k230_dw_ssi_update_irq(s);
        }
        break;
    case A_RXFLR:
        value = fifo32_num_used(&s->rx_fifo);
        break;
    case A_SR:
        value = k230_dw_ssi_status(s);
        if (s->phase == K230_DW_SSI_PHASE_STANDARD_TX_ONLY) {
            k230_dw_ssi_run_transfer(s);
            k230_dw_ssi_update_irq(s);
        }
        break;
    case A_ISR:
        value = k230_dw_ssi_irq_raw_status(s) & s->regs[R_IMR] &
                K230_DW_SSI_IRQ_VALID_MASK;
        break;
    case A_RISR:
        value = k230_dw_ssi_irq_raw_status(s);
        break;
    case A_TXEICR:
        value = k230_dw_ssi_irq_read_clear(
            s, R_RISR_TXOIR_MASK | R_RISR_TXUIR_MASK);
        break;
    case A_RXOICR:
        value = k230_dw_ssi_irq_read_clear(s, R_RISR_RXOIR_MASK);
        break;
    case A_RXUICR:
        value = k230_dw_ssi_irq_read_clear(s, R_RISR_RXUIR_MASK);
        break;
    case A_MSTICR:
        value = k230_dw_ssi_irq_read_clear(s, R_RISR_MSTIR_MASK);
        break;
    case A_ICR:
        value = k230_dw_ssi_irq_read_clear(
            s, R_RISR_TXOIR_MASK | R_RISR_RXUIR_MASK |
               R_RISR_RXOIR_MASK | R_RISR_MSTIR_MASK);
        break;
    case A_AXIECR:
        value = k230_dw_ssi_irq_read_clear(s, R_RISR_AXIER_MASK);
        break;
    case A_DONECR:
        value = k230_dw_ssi_irq_read_clear(s, R_RISR_DONER_MASK);
        break;
    default:
        if (addr >= K230_DW_SSI_REGS_SIZE || (addr & 0x3) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                          DEVICE(s)->canonical_path, addr);
        }
        break;
    }

    return value;
}

static void k230_dw_ssi_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    K230DwSsiState *s = K230_DW_SSI(opaque);

    if (k230_dw_ssi_is_dr(addr)) {
        if (k230_dw_ssi_idma_enabled(s)) {
            return;
        }

        k230_dw_ssi_push_tx(s, value);
        return;
    }

    if (k230_dw_ssi_is_razwi(addr)) {
        return;
    }

    if (k230_dw_ssi_write_requires_disabled(addr) &&
        k230_dw_ssi_enabled(s)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to offset 0x%" HWADDR_PRIx
                      " while SSI is enabled\n",
                      DEVICE(s)->canonical_path, addr);
        return;
    }

    switch (addr) {
    case A_CTRLR0:
        k230_dw_ssi_write_masked(s, R_CTRLR0, value,
                                 K230_DW_SSI_CTRLR0_WRITABLE_MASK);
        break;
    case A_CTRLR1:
        k230_dw_ssi_write_masked(s, R_CTRLR1, value,
                                 K230_DW_SSI_CTRLR1_WRITABLE_MASK);
        break;
    case A_SSIENR: {
        bool old_enabled = k230_dw_ssi_enabled(s);
        bool new_enabled = value & R_SSIENR_SSIC_EN_MASK;

        if (old_enabled == new_enabled) {
            return;
        }

        s->regs[R_SSIENR] = FIELD_DP32(0, SSIENR, SSIC_EN, new_enabled);
        if (!new_enabled) {
            k230_dw_ssi_abort_transfer(s);
            return;
        }

        k230_dw_ssi_update_cs(s);
        if (k230_dw_ssi_idma_enabled(s)) {
            k230_dw_ssi_try_idma(s);
        } else {
            k230_dw_ssi_run_transfer(s);
        }
        k230_dw_ssi_update_irq(s);
        break;
    }
    case A_MWCR:
        k230_dw_ssi_write_masked(s, R_MWCR, value,
                                 K230_DW_SSI_MWCR_WRITABLE_MASK);
        break;
    case A_SER: {
        uint32_t old_ser = s->regs[R_SER];

        s->regs[R_SER] = value & MAKE_64BIT_MASK(0, s->num_cs);
        if (old_ser && !s->regs[R_SER]) {
            k230_dw_ssi_abort_transfer(s);
            break;
        }

        k230_dw_ssi_update_cs(s);
        if (k230_dw_ssi_idma_enabled(s)) {
            k230_dw_ssi_try_idma(s);
        } else {
            k230_dw_ssi_run_transfer(s);
        }
        k230_dw_ssi_update_irq(s);
        break;
    }
    case A_BAUDR:
        k230_dw_ssi_write_masked(s, R_BAUDR, value,
                                 K230_DW_SSI_BAUDR_WRITABLE_MASK);
        break;
    case A_TXFTLR:
        k230_dw_ssi_write_masked(s, R_TXFTLR, value,
                                 K230_DW_SSI_TXFTLR_WRITABLE_MASK);
        k230_dw_ssi_update_irq(s);
        break;
    case A_RXFTLR:
        k230_dw_ssi_write_masked(s, R_RXFTLR, value,
                                 K230_DW_SSI_RXFTLR_WRITABLE_MASK);
        k230_dw_ssi_update_irq(s);
        break;
    case A_TXFLR:
    case A_RXFLR:
    case A_SR:
        break;
    case A_IMR:
        k230_dw_ssi_write_masked(s, R_IMR, value,
                                 K230_DW_SSI_IMR_WRITABLE_MASK);
        k230_dw_ssi_update_irq(s);
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
    case A_DMACR:
        k230_dw_ssi_write_masked(s, R_DMACR, value,
                                 K230_DW_SSI_DMACR_WRITABLE_MASK);
        k230_dw_ssi_try_idma(s);
        break;
    case A_AXIAWLEN:
        k230_dw_ssi_write_masked(s, R_AXIAWLEN, value,
                                 K230_DW_SSI_AXIAWLEN_WRITABLE_MASK);
        break;
    case A_AXIARLEN:
        k230_dw_ssi_write_masked(s, R_AXIARLEN, value,
                                 K230_DW_SSI_AXIARLEN_WRITABLE_MASK);
        break;
    case A_IDR:
    case A_SSIC_VERSION_ID:
        break;
    case A_RX_SAMPLE_DELAY:
        k230_dw_ssi_write_masked(
            s, R_RX_SAMPLE_DELAY, value,
            K230_DW_SSI_RX_SAMPLE_DELAY_WRITABLE_MASK);
        break;
    case A_SPI_CTRLR0:
        k230_dw_ssi_write_masked(
            s, R_SPI_CTRLR0, value,
            K230_DW_SSI_SPI_CTRLR0_WRITABLE_MASK);
        break;
    case A_DDR_DRIVE_EDGE:
        k230_dw_ssi_write_masked(
            s, R_DDR_DRIVE_EDGE, value,
            K230_DW_SSI_DDR_DRIVE_EDGE_WRITABLE_MASK);
        break;
    case A_XIP_MODE_BITS:
        k230_dw_ssi_write_masked(
            s, R_XIP_MODE_BITS, value,
            K230_DW_SSI_XIP_MODE_BITS_WRITABLE_MASK);
        break;
    case A_XIP_INCR_INST:
        k230_dw_ssi_write_masked(
            s, R_XIP_INCR_INST, value,
            K230_DW_SSI_XIP_INCR_INST_WRITABLE_MASK);
        break;
    case A_XIP_WRAP_INST:
        k230_dw_ssi_write_masked(
            s, R_XIP_WRAP_INST, value,
            K230_DW_SSI_XIP_WRAP_INST_WRITABLE_MASK);
        break;
    case A_SPIDR:
        k230_dw_ssi_write_masked(s, R_SPIDR, value,
                                 K230_DW_SSI_SPIDR_WRITABLE_MASK);
        break;
    case A_SPIAR:
        k230_dw_ssi_write_masked(s, R_SPIAR, value,
                                 K230_DW_SSI_SPIAR_WRITABLE_MASK);
        break;
    case A_AXIAR0:
        k230_dw_ssi_write_masked(s, R_AXIAR0, value,
                                 K230_DW_SSI_AXIAR0_WRITABLE_MASK);
        break;
    case A_AXIAR1:
        k230_dw_ssi_write_masked(s, R_AXIAR1, value,
                                 K230_DW_SSI_AXIAR1_WRITABLE_MASK);
        break;
    case A_AXIECR:
    case A_DONECR:
        break;
    default:
        if (addr >= K230_DW_SSI_REGS_SIZE || (addr & 0x3) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                          DEVICE(s)->canonical_path, addr);
        }
        break;
    }
}

static const MemoryRegionOps k230_dw_ssi_ops = {
    .read = k230_dw_ssi_read,
    .write = k230_dw_ssi_write,
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

static void k230_dw_ssi_enter_reset(Object *obj, ResetType type)
{
    K230DwSsiState *s = K230_DW_SSI(obj);

    memset(s->regs, 0, sizeof(s->regs));
    fifo32_reset(&s->tx_fifo);
    fifo32_reset(&s->rx_fifo);
    s->phase = K230_DW_SSI_PHASE_IDLE;
    s->remaining_frames = 0;
    s->irq_latched = 0;
    s->idma_completed_frames = 0;
    memset(&s->enhanced, 0, sizeof(s->enhanced));

    s->regs[R_CTRLR0] = K230_DW_SSI_CTRLR0_RESET;
    s->regs[R_SR] = K230_DW_SSI_SR_RESET;
    s->regs[R_IMR] = K230_DW_SSI_IMR_RESET;
    s->regs[R_AXIAWLEN] = K230_DW_SSI_AXIAWLEN_RESET;
    s->regs[R_AXIARLEN] = K230_DW_SSI_AXIARLEN_RESET;
    s->regs[R_IDR] = K230_DW_SSI_IDR_RESET;
    s->regs[R_SSIC_VERSION_ID] = K230_DW_SSI_VERSION;
    s->regs[R_SPI_CTRLR0] = s->max_lines == 8 ?
        K230_DW_SSI_SPI_CTRLR0_FMC_RESET :
        K230_DW_SSI_SPI_CTRLR0_SPI_RESET;

    k230_dw_ssi_update_irq(s);
}

static void k230_dw_ssi_hold_reset(Object *obj, ResetType type)
{
    K230DwSsiState *s = K230_DW_SSI(obj);
    s->active_cs = -1;

    if (s->cs_lines) {
        for (int i = 0; i < s->num_cs; i++) {
            qemu_irq_raise(s->cs_lines[i]);
        }
    }
}

static void k230_dw_ssi_exit_reset(Object *obj, ResetType type)
{
    K230DwSsiState *s = K230_DW_SSI(obj);

    k230_dw_ssi_update_irq(s);
}

static int k230_dw_ssi_post_load(void *opaque, int version_id)
{
    K230DwSsiState *s = opaque;

    if (s->active_cs < -1 || s->active_cs >= (int)s->num_cs) {
        return -EINVAL;
    }

    for (int i = 0; i < s->num_cs; i++) {
        qemu_irq_raise(s->cs_lines[i]);
    }
    if (s->active_cs >= 0) {
        qemu_irq_lower(s->cs_lines[s->active_cs]);
    }

    k230_dw_ssi_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_k230_dw_ssi = {
    .name = TYPE_K230_DW_SSI,
    .post_load = k230_dw_ssi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, K230DwSsiState, K230_DW_SSI_NUM_REGS),
        VMSTATE_FIFO32(tx_fifo, K230DwSsiState),
        VMSTATE_FIFO32(rx_fifo, K230DwSsiState),
        VMSTATE_UINT32(irq_latched, K230DwSsiState),
        VMSTATE_UINT32(idma_completed_frames, K230DwSsiState),
        VMSTATE_UINT32(phase, K230DwSsiState),
        VMSTATE_UINT32(remaining_frames, K230DwSsiState),
        VMSTATE_UINT32(enhanced.instruction, K230DwSsiState),
        VMSTATE_UINT32(enhanced.address, K230DwSsiState),
        VMSTATE_UINT32(enhanced.mode, K230DwSsiState),
        VMSTATE_UINT32(enhanced.mode_bits, K230DwSsiState),
        VMSTATE_UINT32(enhanced.instruction_bits, K230DwSsiState),
        VMSTATE_UINT32(enhanced.address_bits, K230DwSsiState),
        VMSTATE_UINT32(enhanced.wait_cycles, K230DwSsiState),
        VMSTATE_UINT32(enhanced.data_frames, K230DwSsiState),
        VMSTATE_UINT32(enhanced.spi_frf, K230DwSsiState),
        VMSTATE_UINT32(enhanced.trans_type, K230DwSsiState),
        VMSTATE_UINT32(enhanced.tmod, K230DwSsiState),
        VMSTATE_BOOL(enhanced.mode_bits_enabled, K230DwSsiState),
        VMSTATE_INT32(active_cs, K230DwSsiState),
        VMSTATE_END_OF_LIST()
    },
};

static void k230_dw_ssi_init(Object *obj)
{
    K230DwSsiState *s = K230_DW_SSI(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->spi = ssi_create_bus(dev, "spi");

    memory_region_init_io(&s->mmio, obj, &k230_dw_ssi_ops, s,
                          TYPE_K230_DW_SSI, K230_DW_SSI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    for (int i = 0; i < K230_DW_SSI_IRQ_COUNT; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }

    fifo32_create(&s->tx_fifo, K230_DW_SSI_FIFO_CAPACITY);
    fifo32_create(&s->rx_fifo, K230_DW_SSI_FIFO_CAPACITY);
    s->active_cs = -1;
}

static void k230_dw_ssi_realize(DeviceState *dev, Error **errp)
{
    K230DwSsiState *s = K230_DW_SSI(dev);

    if (s->num_cs == 0 || s->num_cs > 8) {
        error_setg(errp, "%s: num-cs must be in range 1..8",
                   dev->canonical_path);
        return;
    }

    if (s->max_lines != 1 && s->max_lines != 4 && s->max_lines != 8) {
        error_setg(errp, "%s: max-lines must be 1, 4, or 8",
                   dev->canonical_path);
        return;
    }

    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    qdev_init_gpio_out_named(dev, s->cs_lines, "cs", s->num_cs);
}

static void k230_dw_ssi_finalize(Object *obj)
{
    K230DwSsiState *s = K230_DW_SSI(obj);

    fifo32_destroy(&s->tx_fifo);
    fifo32_destroy(&s->rx_fifo);
    g_free(s->cs_lines);
}

static const Property k230_dw_ssi_properties[] = {
    DEFINE_PROP_UINT32("num-cs", K230DwSsiState, num_cs, 1),
    DEFINE_PROP_UINT32("max-lines", K230DwSsiState, max_lines, 1),
};

static void k230_dw_ssi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_dw_ssi_realize;
    dc->vmsd = &vmstate_k230_dw_ssi;
    device_class_set_props(dc, k230_dw_ssi_properties);
    rc->phases.enter = k230_dw_ssi_enter_reset;
    rc->phases.hold = k230_dw_ssi_hold_reset;
    rc->phases.exit = k230_dw_ssi_exit_reset;
}

static const TypeInfo k230_dw_ssi_info = {
    .name = TYPE_K230_DW_SSI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230DwSsiState),
    .instance_init = k230_dw_ssi_init,
    .instance_finalize = k230_dw_ssi_finalize,
    .class_init = k230_dw_ssi_class_init,
};

static void k230_dw_ssi_register_types(void)
{
    type_register_static(&k230_dw_ssi_info);
}

type_init(k230_dw_ssi_register_types)
