/*
 * QTest for Cadence SDHCI ADMA2 pacing / MMIO coupling on the Microchip
 * PolarFire SoC Icicle Kit (microchip-icicle-kit).
 *
 * Drives the generic SDHCI model (hw/sd/sdhci.c) through the Cadence wrapper
 * directly over MMIO under "-accel qtest", where QEMU_CLOCK_VIRTUAL only
 * advances when the test steps it; no guest firmware runs.
 *
 * Copyright (c) 2026 Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sd/sdhci.h"
#include "hw/sd/sdhci-internal.h"

#include "libqtest.h"
#include "libqos/sdhci-cmd.h"

/*
 * Use the controller definitions and command helper shared with the existing
 * SDHCI qtests so this reproducer stays in sync with the device model.
 */

/* Cadence maps the generic SDHCI (SRS) register window at base + 0x200 */
#define SDHCI_BASE        (0x20008000ULL + 0x200)

/* Scratch in PolarFire DRAM (base 0x80000000): descriptor table + target */
#define ADMA_TABLE_ADDR   0x82000000ULL
#define ADMA_BUF_ADDR     0x82100000ULL

#define BLK_LEN           512
#define NDESC             12      /* > SDHC_ADMA_DESCS_PER_DELAY (5) */
#define INT_DESC          5       /* ADMA interrupt descriptor */
#define TRANSFER_SIZE     (NDESC * BLK_LEN)

/* CMDREG response-type encodings (bits [1:0]) */
#define RESP_NONE         0x0000
#define RESP_R2           0x0001  /* 136-bit */
#define RESP_R48          0x0002  /* 48-bit */
#define RESP_R1B          0x0003  /* 48-bit with busy */

#define ACMD41_ARG        0x40FF8000u   /* HCS + 3.3-3.6V window */
#define CMD8_ARG          0x000001AAu   /* VHS 2.7-3.6V + check pattern */

static void fill_pattern(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] = (i * 13 + 7) & 0xff;
    }
}

static void write_pattern(int fd)
{
    g_autofree uint8_t *buf = g_malloc(TRANSFER_SIZE);
    ssize_t written;

    fill_pattern(buf, TRANSFER_SIZE);
    written = pwrite(fd, buf, TRANSFER_SIZE, 0);
    g_assert_cmpint(written, ==, TRANSFER_SIZE);
}

static void check_adma_data(QTestState *qts)
{
    g_autofree uint8_t *actual = g_malloc(TRANSFER_SIZE);
    g_autofree uint8_t *expected = g_malloc(TRANSFER_SIZE);

    fill_pattern(expected, TRANSFER_SIZE);
    qtest_memread(qts, ADMA_BUF_ADDR, actual, TRANSFER_SIZE);
    g_assert_cmpmem(actual, TRANSFER_SIZE, expected, TRANSFER_SIZE);
}

/* Issue an SD command and wait (bounded) for Command Complete */
static uint16_t sd_cmd(QTestState *qts, uint8_t cmd, uint32_t arg,
                       uint16_t flags)
{
    uint16_t sts = 0;
    int i;

    sdhci_cmd_regs(qts, SDHCI_BASE, 0, 0, arg, 0,
                   ((uint16_t)cmd << 8) | flags);

    for (i = 0; i < 1000; i++) {
        sts = qtest_readw(qts, SDHCI_BASE + SDHC_NORINTSTS);
        if (sts & SDHC_NIS_CMDCMP) {
            break;
        }
    }
    g_assert_cmphex(sts & SDHC_NIS_CMDCMP, ==, SDHC_NIS_CMDCMP);

    qtest_writew(qts, SDHCI_BASE + SDHC_NORINTSTS, 0xffff);  /* w1c */
    return sts;
}

/* Bring the SD card from idle to transfer state */
static void sd_bring_up_card(QTestState *qts)
{
    uint32_t rca;

    g_assert_cmphex(qtest_readl(qts, SDHCI_BASE + SDHC_PRNSTS) &
                    SDHC_CARD_PRESENT, ==, SDHC_CARD_PRESENT);

    qtest_writeb(qts, SDHCI_BASE + SDHC_SWRST, SDHC_RESET_ALL);
    qtest_writew(qts, SDHCI_BASE + SDHC_CLKCON,
                 SDHC_CLOCK_INT_EN | SDHC_CLOCK_SDCLK_EN);
    qtest_writew(qts, SDHCI_BASE + SDHC_NORINTSTSEN, 0xffff);
    qtest_writew(qts, SDHCI_BASE + SDHC_ERRINTSTSEN, 0xffff);
    qtest_writew(qts, SDHCI_BASE + SDHC_NORINTSIGEN, 0xffff);

    sd_cmd(qts, 0,  0x00000000, RESP_NONE);  /* GO_IDLE_STATE */
    sd_cmd(qts, 8,  CMD8_ARG,   RESP_R48);   /* SEND_IF_COND */
    sd_cmd(qts, 55, 0x00000000, RESP_R48);   /* APP_CMD */
    sd_cmd(qts, 41, ACMD41_ARG, RESP_R48);   /* SD_SEND_OP_COND */
    sd_cmd(qts, 2,  0x00000000, RESP_R2);    /* ALL_SEND_CID */
    sd_cmd(qts, 3,  0x00000000, RESP_R48);   /* SEND_RELATIVE_ADDR */

    rca = qtest_readl(qts, SDHCI_BASE + SDHC_RSPREG0) >> 16;  /* R6 */

    sd_cmd(qts, 7,  rca << 16,  RESP_R1B);   /* SELECT_CARD */
    sd_cmd(qts, 16, BLK_LEN,    RESP_R48);   /* SET_BLOCKLEN */
}

/*
 * Build a 32-bit ADMA2 table: per 8-byte entry word0 = (len << 16) | attr,
 * word1 = target address.  When int_desc >= 0 that descriptor also carries
 * the ADMA descriptor interrupt attribute, so the model yields mid-chain
 * (rescheduling on QEMU_CLOCK_VIRTUAL) instead of running to completion.
 */
static void build_adma_table(QTestState *qts, int int_desc)
{
    for (int i = 0; i < NDESC; i++) {
        uint8_t attr = SDHC_ADMA_ATTR_VALID | SDHC_ADMA_ATTR_ACT_TRAN;
        uint32_t addr = (uint32_t)(ADMA_BUF_ADDR + (uint64_t)i * BLK_LEN);

        if (i == int_desc) {
            attr |= SDHC_ADMA_ATTR_INT;
        }
        if (i == NDESC - 1) {
            attr |= SDHC_ADMA_ATTR_END;
        }
        qtest_writel(qts, ADMA_TABLE_ADDR + (uint64_t)i * 8,
                     ((uint32_t)BLK_LEN << 16) | attr);
        qtest_writel(qts, ADMA_TABLE_ADDR + (uint64_t)i * 8 + 4, addr);
    }
}

/* Program the ADMA2 engine, descriptor pointer and transfer geometry */
static void program_adma_read(QTestState *qts)
{
    qtest_writeb(qts, SDHCI_BASE + SDHC_HOSTCTL, SDHC_CTRL_ADMA2_32);
    qtest_writel(qts, SDHCI_BASE + SDHC_ADMASYSADDR, (uint32_t)ADMA_TABLE_ADDR);
    qtest_writel(qts, SDHCI_BASE + SDHC_ADMASYSADDR + 4,
                 (uint32_t)(ADMA_TABLE_ADDR >> 32));
    qtest_writel(qts, SDHCI_BASE + SDHC_BLKSIZE, BLK_LEN | (NDESC << 16));
    qtest_writew(qts, SDHCI_BASE + SDHC_TRNMOD,
                 SDHC_TRNS_DMA | SDHC_TRNS_BLK_CNT_EN |
                 SDHC_TRNS_READ | SDHC_TRNS_MULTI);
    qtest_writel(qts, SDHCI_BASE + SDHC_ARGUMENT, 0);  /* start block 0 */
    qtest_writew(qts, SDHCI_BASE + SDHC_NORINTSTS, 0xffff);
    qtest_memset(qts, ADMA_BUF_ADDR, 0xa5, TRANSFER_SIZE);
}

/* CMD18 READ_MULTIPLE_BLOCK with data present -> kicks off ADMA */
static void kick_cmd18(QTestState *qts)
{
    qtest_writew(qts, SDHCI_BASE + SDHC_CMDREG,
                 (18 << 8) | SDHC_CMD_DATA_PRESENT | RESP_R48);
}

/* Start a card-carrying icicle machine and bring the card to "tran" */
static QTestState *icicle_start(char **tmp)
{
    int fd;
    GError *err = NULL;
    QTestState *qts;

    fd = g_file_open_tmp("icicle-sdhci-XXXXXX.raw", tmp, &err);
    g_assert_no_error(err);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 1 * 1024 * 1024), ==, 0);
    write_pattern(fd);
    close(fd);

    qts = qtest_initf("-machine microchip-icicle-kit -accel qtest "
                      "-display none -drive if=sd,file=%s,format=raw", *tmp);
    sd_bring_up_card(qts);
    return qts;
}

/*
 * Facet 1 -- pacing.  A non-interrupt chain completes after a bounded number
 * of SDHC_TRANSFER_DELAY steps.  With 12 descriptors and a quota of five per
 * batch, completion requires exactly three virtual-clock steps.
 */
static void test_adma_pacing(void)
{
    char *tmp = NULL;
    QTestState *qts = icicle_start(&tmp);
    const int expected_steps = DIV_ROUND_UP(NDESC,
                                            SDHC_ADMA_DESCS_PER_DELAY);
    uint16_t sts;
    int steps = 0;

    build_adma_table(qts, -1);
    program_adma_read(qts);
    kick_cmd18(qts);

    while (!((sts = qtest_readw(qts, SDHCI_BASE + SDHC_NORINTSTS)) &
             SDHC_NIS_TRSCMP)) {
        qtest_clock_step(qts, SDHC_TRANSFER_DELAY);
        steps++;
        g_assert_cmpint(steps, <=, expected_steps);
    }

    g_assert_cmpint(steps, ==, expected_steps);
    g_assert_cmphex(sts & SDHC_NIS_TRSCMP, ==, SDHC_NIS_TRSCMP);
    g_assert_cmphex(sts & SDHC_NIS_ERR, ==, 0);
    check_adma_data(qts);

    g_test_message("ADMA %d descriptors: %d clock steps to Transfer Complete",
                   NDESC, steps);

    qtest_quit(qts);
    unlink(tmp);
    g_free(tmp);
}

/*
 * Facet 2 -- MMIO coupling (regression for "Run ADMA independently of MMIO").
 * With the clock frozen, a bare interrupt-status read must not resume the
 * transfer left pending by the mid-chain interrupt descriptor: Transfer
 * Complete must still be clear.  A model that resumes ADMA from an MMIO read
 * completes it on that bare read and fails the assertion.
 */
static void test_adma_mmio_coupling(void)
{
    char *tmp = NULL;
    QTestState *qts = icicle_start(&tmp);
    uint16_t sts;
    int steps = 0;

    build_adma_table(qts, INT_DESC);
    program_adma_read(qts);
    kick_cmd18(qts);

    qtest_clock_step(qts, SDHC_TRANSFER_DELAY);
    qtest_clock_step(qts, SDHC_TRANSFER_DELAY);

    sts = qtest_readw(qts, SDHCI_BASE + SDHC_NORINTSTS);
    g_assert_cmphex(sts & (SDHC_NIS_DMA | SDHC_NIS_TRSCMP), ==,
                    SDHC_NIS_DMA);

    sts = qtest_readw(qts, SDHCI_BASE + SDHC_NORINTSTS);
    g_assert_cmphex(sts & (SDHC_NIS_DMA | SDHC_NIS_TRSCMP), ==,
                    SDHC_NIS_DMA);

    while (!((sts = qtest_readw(qts, SDHCI_BASE + SDHC_NORINTSTS)) &
             SDHC_NIS_TRSCMP)) {
        qtest_clock_step(qts, SDHC_TRANSFER_DELAY);
        steps++;
        g_assert_cmpint(steps, <, 1000);
    }
    g_assert_cmphex(sts & SDHC_NIS_ERR, ==, 0);
    check_adma_data(qts);

    g_test_message("ADMA coupling: %d clock steps to complete after bare read",
                   steps);

    qtest_quit(qts);
    unlink(tmp);
    g_free(tmp);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/microchip/icicle/sdhci/adma-pacing", test_adma_pacing);
    qtest_add_func("/microchip/icicle/sdhci/adma-mmio-coupling",
                   test_adma_mmio_coupling);
    return g_test_run();
}
