/*
 * QTest for AM64x SDHCI ADMA2 virtual-clock pacing
 *
 * CMD18 over a 12-descriptor ADMA2 chain has to complete synchronously
 * when no DMA-boundary interrupt is requested.  A sliced transfer needs
 * at least one SDHC_TRANSFER_DELAY virtual-clock step.
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * Keep the small SDHCI subset local; sdhci-internal.h is not usable from
 * qtests, since it exposes SDHCIState internals.
 */
#define SDHC_ARGUMENT              0x08
#define SDHC_TRNMOD                0x0C
#define SDHC_TRNS_DMA              0x0001
#define SDHC_TRNS_BLK_CNT_EN       0x0002
#define SDHC_TRNS_READ             0x0010
#define SDHC_TRNS_MULTI            0x0020
#define SDHC_CMDREG                0x0E
#define SDHC_CMD_DATA_PRESENT      (1 << 5)
#define SDHC_RSPREG0               0x10
#define SDHC_PRNSTS                0x24
#define SDHC_CARD_PRESENT          0x00010000
#define SDHC_HOSTCTL               0x28
#define SDHC_CTRL_ADMA2_32         0x10
#define SDHC_BLKSIZE               0x04
#define SDHC_CLKCON                0x2C
#define SDHC_CLOCK_INT_EN          0x0001
#define SDHC_CLOCK_SDCLK_EN        (1 << 2)
#define SDHC_SWRST                 0x2F
#define SDHC_RESET_ALL             0x01
#define SDHC_NORINTSTS             0x30
#define SDHC_NIS_ERR               0x8000
#define SDHC_NIS_CMDCMP            0x0001
#define SDHC_NIS_TRSCMP            0x0002
#define SDHC_NORINTSTSEN           0x34
#define SDHC_ERRINTSTSEN           0x36
#define SDHC_ADMASYSADDR           0x58
#define SDHC_ADMA_ATTR_ACT_TRAN    (1 << 5)
#define SDHC_ADMA_ATTR_END         (1 << 1)
#define SDHC_ADMA_ATTR_VALID       (1 << 0)
#define SDHC_TRANSFER_DELAY        100
/* NDESC stays above SDHC_ADMA_DESCS_PER_DELAY (5). */

/*
 * Probe both AM64x SDHCI instances.  The first free sd-bus gets the
 * test card, so the base with CARD_PRESENT depends from realize order.
 */
#define SDHCI_SD_BASE     0x0fa00000ULL
#define SDHCI_EMMC_BASE   0x0fa10000ULL

/* Scratch DDR: ADMA descriptor table plus target buffer. */
#define ADMA_TABLE_ADDR   0x82000000ULL
#define ADMA_BUF_ADDR     0x82100000ULL

#define BLK_LEN           512
#define NDESC             12      /* > SDHC_ADMA_DESCS_PER_DELAY (5) */

/* SDHCI command-register response-type encodings (CMDREG bits [1:0]). */
#define RESP_NONE         0x0000
#define RESP_R2           0x0001  /* 136-bit (CID/CSD) */
#define RESP_R48          0x0002  /* 48-bit  (R1/R3/R6/R7) */
#define RESP_R1B          0x0003  /* 48-bit with busy (R1b) */

/* ACMD41 argument: HCS (bit30) + a 3.3-3.6V voltage window (non-enquiry). */
#define ACMD41_ARG        0x40FF8000u
/* CMD8 argument: VHS = 2.7-3.6V + recommended check pattern 0xAA. */
#define CMD8_ARG          0x000001AAu

/* Bounded command-complete poll; returns actual latched NORINTSTS. */
static uint16_t sd_cmd(QTestState *qts, uint64_t base, uint8_t cmd,
                       uint32_t arg, uint16_t flags)
{
    uint16_t sts = 0;
    int i;

    qtest_writel(qts, base + SDHC_ARGUMENT, arg);
    qtest_writew(qts, base + SDHC_CMDREG, ((uint16_t)cmd << 8) | flags);

    /* CMDREG write runs synchronously sdhci_send_command(). */
    for (i = 0; i < 1000; i++) {
        sts = qtest_readw(qts, base + SDHC_NORINTSTS);
        if (sts & SDHC_NIS_CMDCMP) {
            break;
        }
    }
    g_assert_cmphex(sts & SDHC_NIS_CMDCMP, ==, SDHC_NIS_CMDCMP);

    /* Clear all latched normal-interrupt status bits (write-1-to-clear). */
    qtest_writew(qts, base + SDHC_NORINTSTS, 0xffff);
    return sts;
}

/* Return the controller base whose PRNSTS reports a card inserted. */
static uint64_t find_card_base(QTestState *qts)
{
    uint32_t sd_sts = qtest_readl(qts, SDHCI_SD_BASE + SDHC_PRNSTS);
    uint32_t emmc_sts = qtest_readl(qts, SDHCI_EMMC_BASE + SDHC_PRNSTS);

    if (sd_sts & SDHC_CARD_PRESENT) {
        return SDHCI_SD_BASE;
    }
    if (emmc_sts & SDHC_CARD_PRESENT) {
        return SDHCI_EMMC_BASE;
    }
    g_assert_not_reached();
}

/* Bring SD card to transfer state. */
static void sd_bring_up_card(QTestState *qts, uint64_t base)
{
    uint32_t rca;

    /* Host reset, clock on (INT_EN sets INT_STABLE), latch all status. */
    qtest_writeb(qts, base + SDHC_SWRST, SDHC_RESET_ALL);
    qtest_writew(qts, base + SDHC_CLKCON,
                 SDHC_CLOCK_INT_EN | SDHC_CLOCK_SDCLK_EN);
    qtest_writew(qts, base + SDHC_NORINTSTSEN, 0xffff);
    qtest_writew(qts, base + SDHC_ERRINTSTSEN, 0xffff);

    sd_cmd(qts, base, 0,  0x00000000, RESP_NONE);  /* CMD0  GO_IDLE_STATE   */
    sd_cmd(qts, base, 8,  CMD8_ARG,   RESP_R48);    /* CMD8  SEND_IF_COND    */

    /* ACMD41: CMD55 (APP_CMD) then CMD41 (SEND_OP_COND), until powered up. */
    for (int i = 0; i < 100; i++) {
        sd_cmd(qts, base, 55, 0x00000000, RESP_R48);
        sd_cmd(qts, base, 41, ACMD41_ARG, RESP_R48);
        /* Non-enquiry ACMD41 powers up the model; the loop is a guard. */
        break;
    }

    sd_cmd(qts, base, 2, 0x00000000, RESP_R2);     /* CMD2  ALL_SEND_CID    */
    sd_cmd(qts, base, 3, 0x00000000, RESP_R48);    /* CMD3  SEND_RELATIVE   */

    /* R6 packs the assigned RCA in the upper 16 bits of RSPREG0. */
    rca = qtest_readl(qts, base + SDHC_RSPREG0) >> 16;

    sd_cmd(qts, base, 7, rca << 16, RESP_R1B);     /* CMD7  SELECT_CARD     */
    sd_cmd(qts, base, 16, BLK_LEN,  RESP_R48);     /* CMD16 SET_BLOCKLEN    */
}

/*
 * 32-bit ADMA2 entries are little-endian 64-bit words: attr in bits
 * [6:0], length in [31:16], address in [63:32].  No INT bit is set,
 * so we expect the whole chain in one call.
 */
static void build_adma_table(QTestState *qts)
{
    for (int i = 0; i < NDESC; i++) {
        uint8_t attr = SDHC_ADMA_ATTR_VALID | SDHC_ADMA_ATTR_ACT_TRAN;
        uint32_t addr = (uint32_t)(ADMA_BUF_ADDR + (uint64_t)i * BLK_LEN);

        if (i == NDESC - 1) {
            attr |= SDHC_ADMA_ATTR_END;
        }
        qtest_writel(qts, ADMA_TABLE_ADDR + (uint64_t)i * 8,
                     ((uint32_t)BLK_LEN << 16) | attr);
        qtest_writel(qts, ADMA_TABLE_ADDR + (uint64_t)i * 8 + 4, addr);
    }
}

static void test_adma_pacing(void)
{
    char *tmp = NULL;
    int fd;
    GError *err = NULL;
    uint64_t base;
    QTestState *qts;
    uint16_t sts;
    int steps = 0;

    /* 1 MiB raw backing file, filled with zero. */
    fd = g_file_open_tmp("am64-adma-XXXXXX.raw", &tmp, &err);
    g_assert_no_error(err);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 1 * 1024 * 1024), ==, 0);
    close(fd);

    qts = qtest_initf("-machine am64-virt -accel qtest -display none "
                      "-drive if=none,id=sd0,file=%s,format=raw "
                      "-device sd-card,drive=sd0", tmp);

    base = find_card_base(qts);
    sd_bring_up_card(qts, base);

    /* Select ADMA2 32-bit as DMA engine (Host Control 1 bits [4:3]). */
    qtest_writeb(qts, base + SDHC_HOSTCTL, SDHC_CTRL_ADMA2_32);

    build_adma_table(qts);

    /* Program descriptor/geometry and clear old status. */
    qtest_writel(qts, base + SDHC_ADMASYSADDR, (uint32_t)ADMA_TABLE_ADDR);
    qtest_writel(qts, base + SDHC_ADMASYSADDR + 4,
                 (uint32_t)(ADMA_TABLE_ADDR >> 32));
    qtest_writel(qts, base + SDHC_BLKSIZE, BLK_LEN | (NDESC << 16));
    qtest_writew(qts, base + SDHC_TRNMOD,
                 SDHC_TRNS_DMA | SDHC_TRNS_BLK_CNT_EN |
                 SDHC_TRNS_READ | SDHC_TRNS_MULTI);
    qtest_writel(qts, base + SDHC_ARGUMENT, 0);  /* start block 0 */
    qtest_writew(qts, base + SDHC_NORINTSTS, 0xffff);

    /* CMD18 READ_MULTIPLE_BLOCK kicks off ADMA. */
    qtest_writew(qts, base + SDHC_CMDREG,
                 (18 << 8) | SDHC_CMD_DATA_PRESENT | RESP_R48);

    /*
     * Transfer Complete has to be already set.  A batched 12-descriptor
     * chain needs at least one SDHC_TRANSFER_DELAY step.
     */
    while (!((sts = qtest_readw(qts, base + SDHC_NORINTSTS)) &
             SDHC_NIS_TRSCMP)) {
        qtest_clock_step(qts, SDHC_TRANSFER_DELAY);
        steps++;
        g_assert_cmpint(steps, <, 1000);
    }

    /* Completion has to be Transfer Complete with no error interrupt. */
    g_assert_cmphex(sts & SDHC_NIS_TRSCMP, ==, SDHC_NIS_TRSCMP);
    g_assert_cmphex(sts & SDHC_NIS_ERR, ==, 0);

    g_test_message("ADMA %d descriptors: %d clock steps", NDESC, steps);

    /* The fixed path completes whole chain in one pass: no clock steps. */
    g_assert_cmpint(steps, ==, 0);

    qtest_quit(qts);
    unlink(tmp);
    g_free(tmp);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64/sdhci/adma-pacing", test_adma_pacing);
    return g_test_run();
}
