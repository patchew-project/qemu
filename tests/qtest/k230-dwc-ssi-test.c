/*
 * QTest for the Kendryte K230 DWC SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "qemu/units.h"

#define K230_SPI0_BASE          0x91584000ULL
#define K230_SPI1_BASE          0x91582000ULL
#define K230_SPI2_BASE          0x91583000ULL
#define K230_PLIC_BASE          0xf00000000ULL
#define K230_PLIC_PENDING_BASE  0x1000
#define K230_SSI_CTRLR0          0x000
#define K230_SSI_CTRLR1          0x004
#define K230_SSI_SSIENR          0x008
#define K230_SSI_MWCR            0x00c
#define K230_SSI_SER             0x010
#define K230_SSI_BAUDR           0x014
#define K230_SSI_TXFTLR          0x018
#define K230_SSI_TXFLR           0x020
#define K230_SSI_RXFLR           0x024
#define K230_SSI_SR              0x028
#define K230_SSI_IMR             0x02c
#define K230_SSI_ISR             0x030
#define K230_SSI_RISR            0x034
#define K230_SSI_RXUICR          0x040
#define K230_SSI_IDR             0x058
#define K230_SSI_VERSION_ID      0x05c
#define K230_SSI_DR0             0x060
#define K230_SSI_SPI_CTRLR0      0x0f4
#define K230_SSI_AXIECR          0x130
#define K230_SSI_DONECR          0x134

#define K230_SSI_CTRLR0_RESET           0x00004007U
#define K230_SSI_IDR_RESET              0xa1b2c3d5U
#define K230_SSI_VERSION_RESET          0x3130332aU

#define K230_SSI_CTRLR0_WRITABLE_MASK   0x000f7f1fU
#define K230_SSI_BAUDR_WRITABLE_MASK    0x0000fffeU

#define K230_SSI_CTRLR0_DFS_MASK        0x1fU
#define K230_SSI_CTRLR0_TMOD_SHIFT      10
#define K230_SSI_CTRLR0_SRL             BIT(13)
#define K230_SSI_CTRLR0_SPI_FRF_MASK    (3U << 22)
#define K230_SSI_MWCR_WRITABLE_MASK     0x00000007U
#define K230_SSI_TXFTLR_TXFTHR_SHIFT     16
#define K230_SSI_TMOD_TR                0
#define K230_SSI_TMOD_TO                1
#define K230_SSI_TMOD_RO                2
#define K230_SSI_TMOD_EEPROM_READ       3

#define K230_SSI_SR_BUSY                BIT(0)
#define K230_SSI_SR_TFNF                BIT(1)
#define K230_SSI_SR_TFE                 BIT(2)
#define K230_SSI_SR_RFNE                BIT(3)

#define K230_SSI_INT_TXE                BIT(0)
#define K230_SSI_INT_TXO                BIT(1)
#define K230_SSI_INT_RXO                BIT(3)
#define K230_SSI_INT_RXU                BIT(2)
#define K230_SSI_INT_RXF                BIT(4)
#define K230_SSI_INT_DONE               BIT(11)
#define K230_SSI_INT_AXIE               BIT(8)
#define K230_SSI_IRQ_TXE                0
#define K230_SSI_IRQ_RXU                5
#define K230_SSI_IRQ_DONE               7
#define K230_SSI_IRQ_AXIE               8

#define K230_SSI_RXFTLR                 0x01c
#define K230_SSI_RXOICR                 0x03c
#define K230_SSI_TXEICR                 0x038
#define K230_SSI_ICR                    0x048
#define K230_SSI_DMACR                  0x04c
#define K230_SSI_XIP_MODE_BITS          0x0fc

#define K230_SSI_FIFO_DEPTH             256

typedef struct K230SsiInstance {
    uint64_t base;
    uint32_t num_cs;
    uint32_t imr_reset;
    uint32_t first_irq;
} K230SsiInstance;

static const K230SsiInstance k230_ssi_instances[3] = {
    {
        .base = K230_SPI0_BASE,
        .num_cs = 1,
        .imr_reset = 0x0000003fU,
        .first_irq = 146,
    }, {
        .base = K230_SPI1_BASE,
        .num_cs = 5,
        .imr_reset = 0x0000003fU,
        .first_irq = 155,
    }, {
        .base = K230_SPI2_BASE,
        .num_cs = 5,
        .imr_reset = 0x0000003fU,
        .first_irq = 164,
    },
};

static QTestState *k230_ssi_start(void)
{
    return qtest_init("-machine k230");
}

static uint32_t k230_ssi_readl(QTestState *qts, uint64_t base,
                               uint32_t offset)
{
    return qtest_readl(qts, base + offset);
}

static void k230_ssi_writel(QTestState *qts, uint64_t base,
                            uint32_t offset, uint32_t value)
{
    qtest_writel(qts, base + offset, value);
}

static bool k230_ssi_plic_pending(QTestState *qts, uint32_t irq)
{
    uint64_t addr = K230_PLIC_BASE + K230_PLIC_PENDING_BASE +
                    (irq / 32) * sizeof(uint32_t);

    return qtest_readl(qts, addr) & BIT(irq % 32);
}

static void k230_ssi_disable(QTestState *qts, uint64_t base)
{
    k230_ssi_writel(qts, base, K230_SSI_SSIENR, 0);
}

static void k230_ssi_configure(QTestState *qts, uint64_t base,
                               uint32_t tmod, uint32_t dfs_bits,
                               uint32_t ndf)
{
    uint32_t ctrlr0;

    g_assert_cmpuint(dfs_bits, >=, 4);
    g_assert_cmpuint(dfs_bits, <=, 32);
    g_assert_cmpuint(tmod, <=, K230_SSI_TMOD_EEPROM_READ);

    k230_ssi_disable(qts, base);
    ctrlr0 = (dfs_bits - 1) & K230_SSI_CTRLR0_DFS_MASK;
    ctrlr0 |= tmod << K230_SSI_CTRLR0_TMOD_SHIFT;
    k230_ssi_writel(qts, base, K230_SSI_CTRLR0, ctrlr0);
    k230_ssi_writel(qts, base, K230_SSI_CTRLR1, ndf);
    k230_ssi_writel(qts, base, K230_SSI_BAUDR, 2);
}

static void k230_ssi_enable_cs(QTestState *qts, uint64_t base, uint32_t ser)
{
    k230_ssi_writel(qts, base, K230_SSI_SER, ser);
    k230_ssi_writel(qts, base, K230_SSI_SSIENR, 1);
}

static void k230_ssi_write_frame(QTestState *qts, uint64_t base,
                                 uint32_t value)
{
    k230_ssi_writel(qts, base, K230_SSI_DR0, value);
}

static uint32_t k230_ssi_read_frame(QTestState *qts, uint64_t base)
{
    return k230_ssi_readl(qts, base, K230_SSI_DR0);
}

static void k230_ssi_wait_mask(QTestState *qts, uint64_t base,
                               uint32_t offset, uint32_t mask,
                               uint32_t expected)
{
    for (int i = 0; i < 1000; i++) {
        uint32_t value = k230_ssi_readl(qts, base, offset);

        if ((value & mask) == expected) {
            return;
        }
        qtest_clock_step(qts, 1000);
    }

    g_assert_cmphex(k230_ssi_readl(qts, base, offset) & mask,
                    ==, expected);
}

static void configure_loopback(QTestState *qts, uint32_t tmod,
                               uint32_t ndf)
{
    uint32_t ctrlr0;

    k230_ssi_configure(qts, K230_SPI1_BASE, tmod, 8, ndf);
    ctrlr0 = k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_CTRLR0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_CTRLR0,
                    ctrlr0 | K230_SSI_CTRLR0_SRL);
}

static void test_register_contract(void)
{
    QTestState *qts = k230_ssi_start();

    for (int i = 0; i < ARRAY_SIZE(k230_ssi_instances); i++) {
        const K230SsiInstance *inst = &k230_ssi_instances[i];

        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_CTRLR0),
                        ==, K230_SSI_CTRLR0_RESET);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_SSIENR),
                        ==, 0);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base,
                                      K230_SSI_SPI_CTRLR0),
                        ==, 0);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_IMR),
                        ==, inst->imr_reset);
        k230_ssi_writel(qts, inst->base, K230_SSI_SER, UINT32_MAX);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_SER),
                        ==, MAKE_64BIT_MASK(0, inst->num_cs));
    }

    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_IDR),
                    ==, K230_SSI_IDR_RESET);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE,
                                  K230_SSI_VERSION_ID),
                    ==, K230_SSI_VERSION_RESET);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_CTRLR0, UINT32_MAX);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_CTRLR0),
                    ==, K230_SSI_CTRLR0_WRITABLE_MASK);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_CTRLR0,
                    K230_SSI_CTRLR0_RESET | K230_SSI_CTRLR0_SPI_FRF_MASK);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_CTRLR0) &
                    K230_SSI_CTRLR0_SPI_FRF_MASK, ==, 0);
    /* SSTE is R/W per TRM; the frame-boundary toggle is not modelled. */
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_CTRLR0,
                    K230_SSI_CTRLR0_RESET & ~BIT(14));
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_CTRLR0) &
                    BIT(14), ==, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_BAUDR, UINT32_MAX);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_BAUDR),
                    ==, K230_SSI_BAUDR_WRITABLE_MASK);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_MWCR, UINT32_MAX);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_MWCR),
                    ==, K230_SSI_MWCR_WRITABLE_MASK);

    qtest_system_reset(qts);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_CTRLR0),
                    ==, K230_SSI_CTRLR0_RESET);
    qtest_quit(qts);
}

static void test_pio_data_path(void)
{
    QTestState *qts = k230_ssi_start();
    uint32_t status;

    configure_loopback(qts, K230_SSI_TMOD_TR, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_SR,
                       K230_SSI_SR_RFNE, K230_SSI_SR_RFNE);
    g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0xa5);

    configure_loopback(qts, K230_SSI_TMOD_RO, 3);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, 4);
    for (int i = 0; i < 4; i++) {
        g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0xa5);
    }

    k230_ssi_disable(qts, K230_SPI1_BASE);
    status = k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR);
    g_assert_cmpuint(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                     ==, 0);
    g_assert_cmphex(status & (K230_SSI_SR_BUSY | K230_SSI_SR_TFNF |
                              K230_SSI_SR_TFE | K230_SSI_SR_RFNE),
                    ==, K230_SSI_SR_TFNF | K230_SSI_SR_TFE);
    qtest_quit(qts);
}

static void test_interrupt_controller(void)
{
    QTestState *qts = k230_ssi_start();

    k230_ssi_configure(qts, K230_SPI1_BASE, K230_SSI_TMOD_TR, 8, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_TXFTLR, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_IMR, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SSIENR, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_TXE, ==, K230_SSI_INT_TXE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_TXE, ==, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_IMR,
                    K230_SSI_INT_TXE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_TXE, ==, K230_SSI_INT_TXE);

    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_IMR,
                    K230_SSI_INT_RXU);
    (void)k230_ssi_read_frame(qts, K230_SPI1_BASE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXU, ==, K230_SSI_INT_RXU);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_RXU, ==, K230_SSI_INT_RXU);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXUICR),
                    ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXU, ==, 0);
    qtest_quit(qts);
}

static void test_plic_routing(void)
{
    QTestState *qts = k230_ssi_start();
    const K230SsiInstance *target = &k230_ssi_instances[1];

    for (int i = 0; i < ARRAY_SIZE(k230_ssi_instances); i++) {
        const K230SsiInstance *inst = &k230_ssi_instances[i];

        g_assert_true(k230_ssi_plic_pending(qts,
                                           inst->first_irq +
                                           K230_SSI_IRQ_TXE));
        k230_ssi_writel(qts, inst->base, K230_SSI_IMR, 0);
        g_assert_false(k230_ssi_plic_pending(qts, inst->first_irq +
                                              K230_SSI_IRQ_DONE));
        g_assert_false(k230_ssi_plic_pending(qts, inst->first_irq +
                                              K230_SSI_IRQ_AXIE));
    }

    k230_ssi_writel(qts, target->base, K230_SSI_IMR, K230_SSI_INT_RXU);
    (void)k230_ssi_read_frame(qts, target->base);
    g_assert_true(k230_ssi_plic_pending(qts,
                                       target->first_irq +
                                       K230_SSI_IRQ_RXU));
    for (int i = 0; i < ARRAY_SIZE(k230_ssi_instances); i++) {
        if (&k230_ssi_instances[i] != target) {
            g_assert_false(k230_ssi_plic_pending(
                qts, k230_ssi_instances[i].first_irq + K230_SSI_IRQ_RXU));
        }
    }
    qtest_quit(qts);
}

static void test_tx_only_mode(void)
{
    QTestState *qts = k230_ssi_start();

    configure_loopback(qts, K230_SSI_TMOD_TO, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x55);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_TFE, ==, K230_SSI_SR_TFE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 0);

    configure_loopback(qts, K230_SSI_TMOD_TO, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_TXFTLR,
                    (K230_SSI_FIFO_DEPTH - 1) <<
                    K230_SSI_TXFTLR_TXFTHR_SHIFT);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SSIENR, 1);
    for (int i = 0; i < K230_SSI_FIFO_DEPTH; i++) {
        k230_ssi_write_frame(qts, K230_SPI1_BASE, i);
    }
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, BIT(0));
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_BUSY, ==, 0);
    qtest_quit(qts);
}

/*
 * An interrupt-driven guest waits for TXE without reading TXFLR or SR.
 * The transfer must already be complete by the time the last frame is
 * queued, since the FIFO drains synchronously on DR writes.
 */
static void test_tx_only_irq_ready(void)
{
    QTestState *qts = k230_ssi_start();

    configure_loopback(qts, K230_SSI_TMOD_TO, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    for (int i = 0; i < 8; i++) {
        k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x11 * (i + 1));
    }

    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_TXE, ==, K230_SSI_INT_TXE);
    qtest_quit(qts);
}

static void test_eeprom_read_contract(void)
{
    QTestState *qts = k230_ssi_start();
    const int frames = K230_SSI_FIFO_DEPTH + 4;

    configure_loopback(qts, K230_SSI_TMOD_EEPROM_READ, 3);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SSIENR, 1);
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x03);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, BIT(0));
    /* EEPROM data must start without a status read driving the engine. */
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_ISR,
                       K230_SSI_INT_RXF, K230_SSI_INT_RXF);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, 4);
    for (int i = 0; i < 4; i++) {
        g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0);
    }

    configure_loopback(qts, K230_SSI_TMOD_EEPROM_READ, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SSIENR, 1);
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x03);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, BIT(0));
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, 1);
    g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0);

    configure_loopback(qts, K230_SSI_TMOD_EEPROM_READ, frames - 1);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SSIENR, 1);
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x03);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, BIT(0));
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, K230_SSI_FIFO_DEPTH);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_BUSY, ==, K230_SSI_SR_BUSY);
    g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, K230_SSI_FIFO_DEPTH);
    for (int i = 1; i < frames; i++) {
        g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0);
    }
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_BUSY, ==, 0);
    qtest_quit(qts);
}

static void test_rx_fifo_overflow(void)
{
    QTestState *qts = k230_ssi_start();

    configure_loopback(qts, K230_SSI_TMOD_TR, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    for (int i = 0; i < K230_SSI_FIFO_DEPTH; i++) {
        k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    }
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_TXFTLR,
                    (K230_SSI_FIFO_DEPTH - 1) <<
                    K230_SSI_TXFTLR_TXFTHR_SHIFT);
    for (int i = 0; i < K230_SSI_FIFO_DEPTH; i++) {
        k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    }
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, K230_SSI_INT_RXO);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_BUSY, ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, K230_SSI_FIFO_DEPTH);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE,
                                  K230_SSI_RXOICR), ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, 0);

    configure_loopback(qts, K230_SSI_TMOD_RO, K230_SSI_FIFO_DEPTH + 3);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_TXFTLR, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_BUSY, ==, K230_SSI_SR_BUSY);
    g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0xa5);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, K230_SSI_FIFO_DEPTH);
    for (int i = 1; i < K230_SSI_FIFO_DEPTH + 4; i++) {
        g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0xa5);
    }
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR) &
                    K230_SSI_SR_BUSY, ==, 0);
    qtest_quit(qts);
}

static void test_txfthr_start_gate(void)
{
    QTestState *qts = k230_ssi_start();

    /* TXFTHR=4: transfer starts only once 5+ frames are queued. */
    k230_ssi_configure(qts, K230_SPI1_BASE, K230_SSI_TMOD_TR, 8, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_TXFTLR, 4 << 16);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));

    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x11);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, 0);

    for (int i = 0; i < 4; i++) {
        k230_ssi_write_frame(qts, K230_SPI1_BASE, 0x22 + i);
    }
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, 5);
    qtest_quit(qts);
}

static void test_baudr_zero(void)
{
    QTestState *qts = k230_ssi_start();

    /* SCKDV=0 disables sclk_out: queued frames must not be shifted out. */
    k230_ssi_configure(qts, K230_SPI1_BASE, K230_SSI_TMOD_TR, 8, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_BAUDR, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));

    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_TXFLR),
                    ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, 0);
    qtest_quit(qts);
}

static void test_ser_terminates_ro(void)
{
    QTestState *qts = k230_ssi_start();

    /*
     * RO transfer with NDF larger than the RX FIFO: after the FIFO
     * fills, SER=0 must terminate the pending transaction.  Re-selecting
     * the slave must not resume receiving the stale remaining frames
     * without a new dummy word.
     */
    k230_ssi_configure(qts, K230_SPI1_BASE, K230_SSI_TMOD_RO, 8, 0xffff);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0);
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, K230_SSI_FIFO_DEPTH);

    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, 0);
    (void)k230_ssi_read_frame(qts, K230_SPI1_BASE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, K230_SSI_FIFO_DEPTH - 1);

    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, BIT(0));
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, K230_SSI_FIFO_DEPTH - 1);

    /* A direct BIT(0) -> BIT(1) switch must also terminate the tx. */
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SER, BIT(1));
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                    ==, K230_SSI_FIFO_DEPTH - 1);
    qtest_quit(qts);
}

static void test_icr_total_clear(void)
{
    QTestState *qts = k230_ssi_start();

    configure_loopback(qts, K230_SSI_TMOD_TR, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    (void)k230_ssi_read_frame(qts, K230_SPI1_BASE); /* latch RXU */
    for (int i = 0; i < K230_SSI_FIFO_DEPTH + 1; i++) {
        k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    }
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXU, ==, K230_SSI_INT_RXU);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE,
                                  K230_SSI_ICR), ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXU, ==, 0);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXO, ==, 0);
    qtest_quit(qts);
}



static void test_unsupported_registers(void)
{
    QTestState *qts = k230_ssi_start();
    static const uint32_t unsupported[] = {
        K230_SSI_DMACR, K230_SSI_XIP_MODE_BITS, K230_SSI_SPI_CTRLR0,
    };

    for (size_t i = 0; i < ARRAY_SIZE(unsupported); i++) {
        uint32_t off = unsupported[i];

        g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, off), ==, 0);
        k230_ssi_writel(qts, K230_SPI1_BASE, off, UINT32_MAX);
        g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, off), ==, 0);
    }
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-dwc-ssi/register-contract", test_register_contract);
    qtest_add_func("/k230-dwc-ssi/pio-data-path", test_pio_data_path);
    qtest_add_func("/k230-dwc-ssi/interrupt-controller",
                   test_interrupt_controller);
    qtest_add_func("/k230-dwc-ssi/plic-routing", test_plic_routing);
    qtest_add_func("/k230-dwc-ssi/tx-only-mode", test_tx_only_mode);
    qtest_add_func("/k230-dwc-ssi/tx-only-irq-ready",
                   test_tx_only_irq_ready);
    qtest_add_func("/k230-dwc-ssi/eeprom-read-contract",
                   test_eeprom_read_contract);
    qtest_add_func("/k230-dwc-ssi/rx-fifo-overflow", test_rx_fifo_overflow);
    qtest_add_func("/k230-dwc-ssi/txfthr-start-gate", test_txfthr_start_gate);
    qtest_add_func("/k230-dwc-ssi/baudr-zero", test_baudr_zero);
    qtest_add_func("/k230-dwc-ssi/ser-terminates-ro", test_ser_terminates_ro);
    qtest_add_func("/k230-dwc-ssi/icr-total-clear", test_icr_total_clear);
    qtest_add_func("/k230-dwc-ssi/unsupported-registers",
                   test_unsupported_registers);
    return g_test_run();
}
