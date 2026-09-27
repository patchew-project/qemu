/*
 * QTest testcase for the STM32L496G-DISCO machine
 *
 * Copyright (c) 2026 EXALT Technologies
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#define QUADSPI_BASE 0xa0001000

#define QUADSPI_CR  (QUADSPI_BASE + 0x00)
#define QUADSPI_DCR (QUADSPI_BASE + 0x04)
#define QUADSPI_SR  (QUADSPI_BASE + 0x08)
#define QUADSPI_FCR (QUADSPI_BASE + 0x0c)
#define QUADSPI_DLR (QUADSPI_BASE + 0x10)
#define QUADSPI_CCR (QUADSPI_BASE + 0x14)
#define QUADSPI_AR  (QUADSPI_BASE + 0x18)
#define QUADSPI_DR  (QUADSPI_BASE + 0x20)
#define QUADSPI_ABR (QUADSPI_BASE + 0x1c)
#define DCR_FSIZE(bits) (((bits) - 1) << 16)

#define CR_EN BIT(0)
#define CR_ABORT BIT(1)
#define CR_TEIE BIT(16)
#define CR_FTHRES(bytes) (((bytes) - 1) << 8)
#define CR_TCIE BIT(17)
#define CR_FTIE BIT(18)

#define SR_TCF BIT(1)
#define SR_TEF BIT(0)
#define SR_FTF BIT(2)
#define SR_BUSY BIT(5)
#define SR_FLEVEL(sr) extract32((sr), 8, 5)

#define FCR_CTCF BIT(1)

#define CCR_IMODE_SINGLE         (1 << 8)
#define CCR_ADMODE_SINGLE        (1 << 10)
#define CCR_ADSIZE_24BIT         (2 << 12)
#define CCR_DCYC(cycles)         ((cycles) << 18)
#define CCR_DMODE_SINGLE         (1 << 24)
#define CCR_FMODE_INDIRECT_READ  (1 << 26)
#define CCR_ADMODE_DUAL          (2 << 10)
#define CCR_ADMODE_QUAD          (3 << 10)
#define CCR_DMODE_DUAL           (2 << 24)
#define CCR_DMODE_QUAD           (3 << 24)
#define CCR_ABMODE_QUAD          (3 << 14)
#define FCR_ALL                  0x1b

#define CMD_WRITE_ENABLE 0x06
#define CMD_WRITE_DISABLE 0x04
#define CMD_READ_STATUS   0x05
#define CMD_READ          0x03
#define CMD_PAGE_PROGRAM  0x02
#define CMD_READ_ID       0x9f
#define CMD_READ_SFDP     0x5a

static QTestState *qts;

static void qspi_command(uint8_t command)
{
    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    qtest_writel(qts, QUADSPI_CCR, CCR_IMODE_SINGLE | command);
}

static void qspi_start_read(uint8_t command, uint32_t address,
                            bool address_phase, unsigned int dummy_cycles,
                            unsigned int length)
{
    uint32_t ccr = CCR_IMODE_SINGLE | CCR_DMODE_SINGLE |
                   CCR_FMODE_INDIRECT_READ | command;

    if (address_phase) {
        ccr |= CCR_ADMODE_SINGLE | CCR_ADSIZE_24BIT;
    }
    ccr |= CCR_DCYC(dummy_cycles);

    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    qtest_writel(qts, QUADSPI_CCR, 0);
    qtest_writel(qts, QUADSPI_DLR, length - 1);
    qtest_writel(qts, QUADSPI_CCR, ccr);
    if (address_phase) {
        qtest_writel(qts, QUADSPI_AR, address);
    }
}

static uint32_t qspi_read(uint8_t command, uint32_t address,
                          bool address_phase, unsigned int dummy_cycles,
                          unsigned int length)
{
    qspi_start_read(command, address, address_phase, dummy_cycles, length);
    return qtest_readl(qts, QUADSPI_DR);
}

static void qspi_start_write(uint32_t address, unsigned int length)
{
    uint32_t ccr = CMD_PAGE_PROGRAM | CCR_IMODE_SINGLE |
                   CCR_ADMODE_SINGLE | CCR_ADSIZE_24BIT |
                   CCR_DMODE_SINGLE;

    qspi_command(CMD_WRITE_ENABLE);
    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    qtest_writel(qts, QUADSPI_CCR, 0);
    qtest_writel(qts, QUADSPI_AR, address);
    qtest_writel(qts, QUADSPI_DLR, length - 1);
    qtest_writel(qts, QUADSPI_CCR, ccr);
}

static void qspi_page_program(uint32_t address, uint32_t value)
{
    qspi_start_write(address, sizeof(value));
    qtest_writel(qts, QUADSPI_DR, value);
}

static void test_quadspi_identification(void)
{
    uint32_t id = qspi_read(CMD_READ_ID, 0, false, 0, 3);

    g_assert_cmphex(id & 0x00ffffff, ==, 0x001728c2);
}

static void test_quadspi_sfdp(void)
{
    uint32_t signature = qspi_read(CMD_READ_SFDP, 0, true, 8, 4);

    g_assert_cmphex(signature, ==, 0x50444653);
}

static void test_quadspi_program_read(void)
{
    const uint32_t address = 0x100;
    const uint32_t value = 0x5aa55aa5;
    uint32_t actual;

    qspi_page_program(address, value);
    actual = qspi_read(CMD_READ, address, true, 0, sizeof(value));

    g_assert_cmphex(actual, ==, value);
}

/* Verify FIFO capacity, RX backpressure, threshold IRQs, and byte ordering. */
static void test_quadspi_fifo_refill(void)
{
    const uint32_t address = 0x200;
    const unsigned int length = 40;
    uint32_t sr;

    qspi_start_write(address, length);
    for (unsigned int i = 0; i < length; i++) {
        qtest_writeb(qts, QUADSPI_DR, i);
    }
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_FTHRES(16) | CR_FTIE);
    qspi_start_read(CMD_READ, address, true, 0, length);
    sr = qtest_readl(qts, QUADSPI_SR);
    g_assert_cmpuint(SR_FLEVEL(sr), ==, 16);
    g_assert_true(sr & SR_BUSY);
    g_assert_true(sr & SR_FTF);
    g_assert_false(sr & SR_TCF);
    g_assert_true(qtest_get_irq(qts, 0));

    g_assert_cmphex(qtest_readl(qts, QUADSPI_DR), ==, 0x03020100);
    sr = qtest_readl(qts, QUADSPI_SR);
    g_assert_cmpuint(SR_FLEVEL(sr), ==, 12);
    g_assert_false(sr & SR_FTF);
    g_assert_false(qtest_get_irq(qts, 0));

    g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, 4);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 16);
    g_assert_true(qtest_get_irq(qts, 0));
    for (unsigned int i = 5; i < length; i++) {
        g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, i);
        sr = qtest_readl(qts, QUADSPI_SR);
        g_assert_cmpuint(SR_FLEVEL(sr), <=, 16);
        if (i + 1 < length) {
            g_assert_true(sr & SR_BUSY);
        }
    }
    g_assert_cmpuint(SR_FLEVEL(sr), ==, 0);
    g_assert_true(sr & SR_TCF);
    g_assert_false(sr & (SR_BUSY | SR_FTF));
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_CR, CR_EN);
}

/* A short final RX fragment must remain readable below the FIFO threshold. */
static void test_quadspi_fifo_tail(void)
{
    uint32_t sr;

    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_FTHRES(16) | CR_FTIE);
    qspi_start_read(CMD_READ_ID, 0, false, 0, 3);
    sr = qtest_readl(qts, QUADSPI_SR);
    g_assert_cmpuint(SR_FLEVEL(sr), ==, 3);
    g_assert_true(sr & SR_TCF);
    g_assert_true(sr & SR_BUSY);
    g_assert_true(sr & SR_FTF);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    g_assert_cmphex(qtest_readw(qts, QUADSPI_DR), ==, 0x28c2);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 1);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readb(qts, QUADSPI_DR), ==, 0x17);
    sr = qtest_readl(qts, QUADSPI_SR);
    g_assert_false(sr & (SR_BUSY | SR_FTF | SR_TCF));
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, 0);
    qtest_writel(qts, QUADSPI_CR, CR_EN);
}

/* Byte, halfword, and word accesses must preserve the FIFO byte sequence. */
static void test_quadspi_fifo_mixed_access(void)
{
    qspi_start_write(0x300, 7);
    qtest_writeb(qts, QUADSPI_DR, 0xa1);
    qtest_writew(qts, QUADSPI_DR, 0xc3b2);
    qtest_writel(qts, QUADSPI_DR, 0x07f6e5d4);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 0);
    qspi_start_read(CMD_READ, 0x300, true, 0, 7);
    g_assert_cmphex(qtest_readb(qts, QUADSPI_DR), ==, 0xa1);
    g_assert_cmphex(qtest_readw(qts, QUADSPI_DR), ==, 0xc3b2);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_DR), ==, 0x07f6e5d4);
}

/* Extra bytes in a final word write must not be programmed beyond DLR. */
static void test_quadspi_fifo_write_excess(void)
{
    qspi_start_write(0x400, 3);
    qtest_writel(qts, QUADSPI_DR, 0x44332211);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 0);
    g_assert_cmphex(qspi_read(CMD_READ, 0x400, true, 0, 4), ==, 0xff332211);
}

/* Aborting a paused RX transfer must discard its FIFO and permit a new read. */
static void test_quadspi_fifo_abort(void)
{
    uint32_t sr;

    qspi_start_read(CMD_READ, 0x200, true, 0, 40);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 16);
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_ABORT);
    sr = qtest_readl(qts, QUADSPI_SR);
    g_assert_cmpuint(SR_FLEVEL(sr), ==, 0);
    g_assert_false(sr & (SR_BUSY | SR_FTF));
    g_assert_true(sr & SR_TCF);
    g_assert_false(qtest_readl(qts, QUADSPI_CR) & CR_ABORT);
    g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, 0);
    g_assert_cmphex(qspi_read(CMD_READ_ID, 0, false, 0, 3), ==, 0x001728c2);
}

/* Reset must discard buffered RX data and deassert the threshold interrupt. */
static void test_quadspi_fifo_reset(void)
{
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_FTIE);
    qspi_start_read(CMD_READ, 0x200, true, 0, 40);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_DLR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(23));
    g_assert_cmphex(qspi_read(CMD_READ_ID, 0, false, 0, 3), ==, 0x001728c2);
}

/* Disabled command writes must not set the flash's write-enable latch. */
static void test_quadspi_disabled_command(void)
{
    qspi_command(CMD_WRITE_DISABLE);
    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    qtest_writel(qts, QUADSPI_CR, CR_TCIE | CR_FTIE);
    qspi_command(CMD_WRITE_ENABLE);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CCR), ==,
                    CCR_IMODE_SINGLE | CMD_WRITE_ENABLE);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, QUADSPI_CR, CR_EN);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmphex(qspi_read(CMD_READ_STATUS, 0, false, 0, 1) & 2, ==, 0);
    qspi_command(CMD_WRITE_ENABLE);
    g_assert_cmphex(qspi_read(CMD_READ_STATUS, 0, false, 0, 1) & 2, ==, 2);
    qspi_command(CMD_WRITE_DISABLE);
}

/* CCR, AR, and DR accesses must not exchange flash data while disabled. */
static void test_quadspi_disabled_data(void)
{
    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    qtest_writel(qts, QUADSPI_CR, CR_TCIE | CR_FTIE);
    qspi_start_read(CMD_READ_ID, 0, false, 0, 3);
    g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    qspi_start_read(CMD_READ, 0x500, true, 0, 4);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_AR), ==, 0x500);
    g_assert_cmpuint(qtest_readl(qts, QUADSPI_DLR), ==, 3);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_DR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);

    qspi_start_write(0x500, 4);
    qtest_writel(qts, QUADSPI_DR, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    qtest_writel(qts, QUADSPI_DR, 0x12345678);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmphex(qspi_read(CMD_READ, 0x500, true, 0, 4), ==, 0xffffffff);
    g_assert_cmphex(qspi_read(CMD_READ_ID, 0, false, 0, 3), ==, 0x001728c2);
}

/* Disable clears RX state without synthesizing completion or replaying CCR. */
static void test_quadspi_disable_read(void)
{
    uint32_t ccr;

    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_FTIE | CR_TCIE);
    qspi_start_read(CMD_READ, 0, true, 0, 40);
    ccr = qtest_readl(qts, QUADSPI_CCR);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 16);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_CR, CR_FTIE | CR_TCIE);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CCR), ==, ccr);
    g_assert_cmpuint(qtest_readl(qts, QUADSPI_DLR), ==, 39);
    g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, 0);
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_FTIE | CR_TCIE);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));

    qspi_start_read(CMD_READ_ID, 0, false, 0, 3);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    qtest_writel(qts, QUADSPI_CR, CR_TCIE);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, SR_TCF);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_FCR, FCR_CTCF);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, QUADSPI_CR, CR_EN);
}

/* Disable stops a partial write without replaying data on re-enable. */
static void test_quadspi_disable_write(void)
{
    uint32_t ccr;

    qspi_start_write(0x600, 4);
    qtest_writeb(qts, QUADSPI_DR, 0xa5);
    ccr = qtest_readl(qts, QUADSPI_CCR);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_BUSY);
    qtest_writel(qts, QUADSPI_CR, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CCR), ==, ccr);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_AR), ==, 0x600);
    qtest_writel(qts, QUADSPI_DR, 0);
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    qtest_writel(qts, QUADSPI_DR, 0);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmphex(qspi_read(CMD_READ, 0x600, true, 0, 4), ==, 0xffffffa5);
}

/* Reject out-of-range reads/writes before exchanging any flash payload. */
static void test_quadspi_address_bounds(void)
{
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(12));
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_TEIE);
    qspi_start_read(CMD_READ, 0x1000, true, 0, 1);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TEF);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & (SR_BUSY | SR_TCF));
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writeb(qts, QUADSPI_FCR, SR_TEF);
    g_assert_false(qtest_get_irq(qts, 0));
    qspi_start_read(CMD_READ, 0xfff, true, 0, 2);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TEF);
    qtest_writel(qts, QUADSPI_FCR, FCR_ALL);
    qspi_start_write(0xfff, 2);
    qtest_writew(qts, QUADSPI_DR, 0x1234);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TEF);
    qtest_writel(qts, QUADSPI_FCR, FCR_ALL);
    g_assert_cmphex(qspi_read(CMD_READ, 0xffc, true, 0, 4), ==, 0xffffffff);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(32));
    qspi_start_read(0x13, UINT32_MAX, true, 0, 2);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TEF);
    qtest_writel(qts, QUADSPI_FCR, FCR_ALL);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(23));
    qtest_writel(qts, QUADSPI_CR, CR_EN);
}

/* Undefined length ends at FSIZE, or continues until abort for FSIZE = 31. */
static void test_quadspi_undefined_length(void)
{
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(12));
    qspi_start_read(CMD_READ, 0xffc, true, 0, 0);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmpuint(SR_FLEVEL(qtest_readl(qts, QUADSPI_SR)), ==, 4);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_DR), ==, 0xffffffff);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_BUSY);
    qspi_start_write(0xffc, 0);
    qtest_writel(qts, QUADSPI_DR, 0x78563412);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmphex(qspi_read(CMD_READ, 0xffc, true, 0, 4), ==, 0x78563412);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(32));
    qspi_start_read(CMD_READ, 0, true, 0, 0);
    for (unsigned int i = 0; i < 12; i++) {
        qtest_readl(qts, QUADSPI_DR);
        g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_BUSY);
        g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    }
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_ABORT);
    qspi_start_write(0x900, 0);
    qtest_writel(qts, QUADSPI_DR, 0x12345678);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_BUSY);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_ABORT);
    g_assert_cmphex(qspi_read(CMD_READ, 0x900, true, 0, 4), ==, 0x12345678);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(23));
}

/* Subword register accesses preserve adjacent bytes and write-one clears. */
static void test_quadspi_partial_registers(void)
{
    qtest_writel(qts, QUADSPI_ABR, 0x12345678);
    qtest_writeb(qts, QUADSPI_ABR + 1, 0xab);
    qtest_writew(qts, QUADSPI_ABR + 2, 0xcdef);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_ABR), ==, 0xcdefab78);
    g_assert_cmphex(qtest_readb(qts, QUADSPI_ABR + 1), ==, 0xab);
    g_assert_cmphex(qtest_readw(qts, QUADSPI_ABR + 2), ==, 0xcdef);
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_TCIE);
    qtest_writeb(qts, QUADSPI_CR + 1, 0x03);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CR), ==,
                    CR_EN | CR_TCIE | CR_FTHRES(4));
    qspi_command(CMD_WRITE_DISABLE);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    qtest_writeb(qts, QUADSPI_FCR + 1, 0xff);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    qtest_writeb(qts, QUADSPI_FCR, FCR_CTCF);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    qtest_writel(qts, QUADSPI_CR, CR_EN);
}

/* A busy controller must not reinterpret its active command configuration. */
static void test_quadspi_busy_registers(void)
{
    uint32_t ccr;

    qspi_start_read(CMD_READ, 0, true, 0, 40);
    ccr = qtest_readl(qts, QUADSPI_CCR);
    qtest_writel(qts, QUADSPI_CCR, CCR_IMODE_SINGLE | CMD_WRITE_ENABLE);
    qtest_writel(qts, QUADSPI_DLR, 1);
    qtest_writel(qts, QUADSPI_AR, 0x800);
    qtest_writel(qts, QUADSPI_DCR, 0);
    qtest_writel(qts, QUADSPI_ABR, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CCR), ==, ccr);
    g_assert_cmpuint(qtest_readl(qts, QUADSPI_DLR), ==, 39);
    g_assert_cmpuint(qtest_readl(qts, QUADSPI_AR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_DCR), ==, DCR_FSIZE(23));
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_FTHRES(16) | CR_FTIE);
    g_assert_cmphex(qtest_readl(qts, QUADSPI_CR), ==, CR_EN | CR_FTIE);
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_ABORT);
}

/* Deferred modes must never issue an accidental WREN command. */
static void test_quadspi_unsupported_modes(void)
{
    static const uint32_t configs[] = {
        (2 << 26), (3 << 26), BIT(28), BIT(31),
    };

    qspi_command(CMD_WRITE_DISABLE);
    for (unsigned int i = 0; i < ARRAY_SIZE(configs); i++) {
        qtest_writel(qts, QUADSPI_FCR, FCR_ALL);
        qtest_writel(qts, QUADSPI_CCR,
                     configs[i] | CCR_IMODE_SINGLE | CMD_WRITE_ENABLE);
        g_assert_false(qtest_readl(qts, QUADSPI_SR) & (SR_TCF | SR_BUSY));
        g_assert_cmphex(qspi_read(CMD_READ_STATUS, 0, false, 0, 1) & 2, ==, 0);
    }
    qtest_writel(qts, QUADSPI_CR, CR_EN | BIT(6));
    qtest_writel(qts, QUADSPI_FCR, FCR_ALL);
    qtest_writel(qts, QUADSPI_CCR, CCR_IMODE_SINGLE | CMD_WRITE_ENABLE);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & (SR_TCF | SR_BUSY));
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    g_assert_cmphex(qspi_read(CMD_READ_STATUS, 0, false, 0, 1) & 2, ==, 0);
}

/* Exercise fast-read opcodes and single/dual/quad byte prefixes. */
static void test_quadspi_lane_modes(void)
{
    static const struct {
        uint8_t opcode;
        uint32_t admode;
        uint32_t dmode;
        unsigned int dummy;
        bool alternate;
    } modes[] = {
        { 0x0b, CCR_ADMODE_SINGLE, CCR_DMODE_SINGLE, 8, false },
        { 0x3b, CCR_ADMODE_SINGLE, CCR_DMODE_DUAL, 8, false },
        { 0x6b, CCR_ADMODE_SINGLE, CCR_DMODE_QUAD, 8, false },
        { 0xbb, CCR_ADMODE_DUAL, CCR_DMODE_DUAL, 4, false },
        { 0xeb, CCR_ADMODE_QUAD, CCR_DMODE_QUAD, 6, false },
        { 0xeb, CCR_ADMODE_QUAD, CCR_DMODE_QUAD, 4, true },
    };

    qspi_page_program(0x800, 0x76543210);
    for (unsigned int i = 0; i < ARRAY_SIZE(modes); i++) {
        uint32_t ccr = CCR_IMODE_SINGLE | CCR_ADSIZE_24BIT |
                       CCR_FMODE_INDIRECT_READ | modes[i].admode |
                       modes[i].dmode | CCR_DCYC(modes[i].dummy) |
                       modes[i].opcode;

        qtest_writel(qts, QUADSPI_ABR, 0);
        if (modes[i].alternate) {
            ccr |= CCR_ABMODE_QUAD;
        }
        qtest_writel(qts, QUADSPI_DLR, 3);
        qtest_writel(qts, QUADSPI_CCR, ccr);
        qtest_writel(qts, QUADSPI_AR, 0x800);
        g_assert_cmphex(qtest_readl(qts, QUADSPI_DR), ==, 0x76543210);
    }
}

static void qspi_wait_migration(QTestState *state)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;

    do {
        QDict *response = qtest_qmp(state, "{'execute': 'query-migrate'}");
        QDict *result = qdict_get_qdict(response, "return");
        const char *status = qdict_get_str(result, "status");
        bool complete = !strcmp(status, "completed");

        if (!strcmp(status, "failed")) {
            g_error("Migration failed: %s",
                    qdict_get_try_str(result, "error-desc"));
        }
        qobject_unref(response);
        if (complete) {
            return;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
    g_error("Migration did not complete within 10 seconds");
}

static void qspi_migrate(void)
{
    char *path = NULL;
    int fd = g_file_open_tmp("qspi-migration-XXXXXX", &path, NULL);
    g_autofree char *uri = g_strdup_printf("file:%s", path);
    QTestState *source = qts;

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    qtest_qmp_assert_success(source,
        "{'execute': 'migrate', 'arguments': {'uri': %s}}", uri);
    qspi_wait_migration(source);
    qts = qtest_init("-machine stm32l496g-disco -incoming defer");
    qtest_qmp_assert_success(qts,
        "{'execute': 'migrate-incoming', 'arguments': {'uri': %s}}", uri);
    qspi_wait_migration(qts);
    qtest_quit(source);
    unlink(path);
    g_free(path);
}

/* SFDP data exposes lost FIFO bytes or a reset flash-parser cursor. */
static void test_quadspi_migration_read(void)
{
    QTestState *original = qts;
    uint8_t expected[40];

    qts = qtest_init("-machine stm32l496g-disco");
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(23));
    qspi_start_read(CMD_READ_SFDP, 0, true, 8, sizeof(expected));
    for (unsigned int i = 0; i < sizeof(expected); i++) {
        expected[i] = qtest_readb(qts, QUADSPI_DR);
    }
    qspi_start_read(CMD_READ_SFDP, 0, true, 8, sizeof(expected));
    for (unsigned int i = 0; i < 3; i++) {
        g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, expected[i]);
    }
    qspi_migrate();
    for (unsigned int i = 3; i < sizeof(expected); i++) {
        g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, expected[i]);
    }
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_BUSY);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(32));
    qspi_start_read(CMD_READ_SFDP, 0, true, 8, 0);
    for (unsigned int i = 0; i < 3; i++) {
        g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, expected[i]);
    }
    qspi_migrate();
    for (unsigned int i = 3; i < sizeof(expected); i++) {
        g_assert_cmpuint(qtest_readb(qts, QUADSPI_DR), ==, expected[i]);
    }
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_BUSY);
    g_assert_false(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    qtest_writel(qts, QUADSPI_CR, CR_EN | CR_ABORT);
    qtest_quit(qts);
    qts = original;
}

/* Restore an unread final fragment and an in-flight register write. */
static void test_quadspi_migration_write(void)
{
    QTestState *original = qts;

    qts = qtest_init("-machine stm32l496g-disco");
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(23));
    qspi_start_read(CMD_READ_ID, 0, false, 0, 3);
    qspi_migrate();
    g_assert_cmphex(qtest_readl(qts, QUADSPI_DR), ==, 0x001728c2);
    qspi_command(CMD_WRITE_ENABLE);
    qtest_writel(qts, QUADSPI_DLR, 1);
    qtest_writel(qts, QUADSPI_CCR,
                 CCR_IMODE_SINGLE | CCR_DMODE_SINGLE | 0x01);
    qspi_migrate();
    qtest_writeb(qts, QUADSPI_DR, 0x40);
    qspi_migrate();
    qtest_writeb(qts, QUADSPI_DR, 0);
    g_assert_true(qtest_readl(qts, QUADSPI_SR) & SR_TCF);
    g_assert_cmphex(qspi_read(CMD_READ_STATUS, 0, false, 0, 1) & 0x40,
                    ==, 0x40);
    qtest_quit(qts);
    qts = original;
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qts = qtest_init("-machine stm32l496g-disco");
    qtest_irq_intercept_out_named(qts, "/machine/soc/quadspi",
                                  "sysbus-irq");
    qtest_writel(qts, QUADSPI_CR, CR_EN);
    qtest_writel(qts, QUADSPI_DCR, DCR_FSIZE(23));

    qtest_add_func("stm32l496g-disco/quadspi/identification",
                   test_quadspi_identification);
    qtest_add_func("stm32l496g-disco/quadspi/sfdp", test_quadspi_sfdp);
    qtest_add_func("stm32l496g-disco/quadspi/program-read",
                   test_quadspi_program_read);
    qtest_add_func("stm32l496g-disco/quadspi/fifo-refill",
                   test_quadspi_fifo_refill);
    qtest_add_func("stm32l496g-disco/quadspi/fifo-tail",
                   test_quadspi_fifo_tail);
    qtest_add_func("stm32l496g-disco/quadspi/fifo-mixed-access",
                   test_quadspi_fifo_mixed_access);
    qtest_add_func("stm32l496g-disco/quadspi/fifo-write-excess",
                   test_quadspi_fifo_write_excess);
    qtest_add_func("stm32l496g-disco/quadspi/fifo-abort",
                   test_quadspi_fifo_abort);
    qtest_add_func("stm32l496g-disco/quadspi/fifo-reset",
                   test_quadspi_fifo_reset);
    qtest_add_func("stm32l496g-disco/quadspi/disabled-command",
                   test_quadspi_disabled_command);
    qtest_add_func("stm32l496g-disco/quadspi/disabled-data",
                   test_quadspi_disabled_data);
    qtest_add_func("stm32l496g-disco/quadspi/disable-read",
                   test_quadspi_disable_read);
    qtest_add_func("stm32l496g-disco/quadspi/disable-write",
                   test_quadspi_disable_write);
    qtest_add_func("stm32l496g-disco/quadspi/address-bounds",
                   test_quadspi_address_bounds);
    qtest_add_func("stm32l496g-disco/quadspi/undefined-length",
                   test_quadspi_undefined_length);
    qtest_add_func("stm32l496g-disco/quadspi/partial-registers",
                   test_quadspi_partial_registers);
    qtest_add_func("stm32l496g-disco/quadspi/busy-registers",
                   test_quadspi_busy_registers);
    qtest_add_func("stm32l496g-disco/quadspi/unsupported-modes",
                   test_quadspi_unsupported_modes);
    qtest_add_func("stm32l496g-disco/quadspi/lane-modes",
                   test_quadspi_lane_modes);
    qtest_add_func("stm32l496g-disco/quadspi/migration-read",
                   test_quadspi_migration_read);
    qtest_add_func("stm32l496g-disco/quadspi/migration-write",
                   test_quadspi_migration_write);

    ret = g_test_run();
    qtest_quit(qts);

    return ret;
}
