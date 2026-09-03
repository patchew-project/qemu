/*
 * QTests for the Synopsys DesignWare Multimedia Card Interface
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "libqtest.h"

#define DW_MCI_BASE             0x28001000

#define DW_MCI_CTRL             0x000
#define DW_MCI_PWREN            0x004
#define DW_MCI_TMOUT            0x014
#define DW_MCI_BLKSIZ           0x01c
#define DW_MCI_BYTCNT           0x020
#define DW_MCI_INTMASK          0x024
#define DW_MCI_CMDARG           0x028
#define DW_MCI_CMD              0x02c
#define DW_MCI_RESP0            0x030
#define DW_MCI_MINTSTS          0x040
#define DW_MCI_RINTSTS          0x044
#define DW_MCI_STATUS           0x048
#define DW_MCI_FIFOTH           0x04c
#define DW_MCI_CDETECT          0x050
#define DW_MCI_TCBCNT           0x05c
#define DW_MCI_TBBCNT           0x060
#define DW_MCI_DEBNCE           0x064
#define DW_MCI_VERID            0x06c
#define DW_MCI_HCON             0x070
#define DW_MCI_RST_N            0x078
#define DW_MCI_BMOD             0x080
#define DW_MCI_PLDMND           0x084
#define DW_MCI_DBADDRL          0x088
#define DW_MCI_DBADDRU          0x08c
#define DW_MCI_IDSTS            0x090
#define DW_MCI_IDINTEN          0x094
#define DW_MCI_DSCADDRL         0x098
#define DW_MCI_DSCADDRU         0x09c
#define DW_MCI_BUFADDRL         0x0a0
#define DW_MCI_BUFADDRU         0x0a4
#define DW_MCI_FIFO             0x200

#define DW_MCI_CTRL_INT_ENABLE  BIT(4)
#define DW_MCI_CTRL_USE_IDMAC   BIT(25)

#define DW_MCI_CMD_RESP_EXP     BIT(6)
#define DW_MCI_CMD_RESP_LONG    BIT(7)
#define DW_MCI_CMD_DAT_EXP      BIT(9)
#define DW_MCI_CMD_DAT_WR       BIT(10)
#define DW_MCI_CMD_UPD_CLK      BIT(21)
#define DW_MCI_CMD_START        BIT(31)

#define DW_MCI_INT_CMD_DONE     BIT(2)
#define DW_MCI_INT_DATA_OVER    BIT(3)
#define DW_MCI_INT_RTO          BIT(8)

#define DW_MCI_BMOD_ENABLE      BIT(7)

#define DW_MCI_IDSTS_TI         BIT(0)
#define DW_MCI_IDSTS_RI         BIT(1)
#define DW_MCI_IDSTS_DU         BIT(4)
#define DW_MCI_IDSTS_NI         BIT(8)
#define DW_MCI_IDSTS_AI         BIT(9)

#define DW_MCI_IDMAC_LD         BIT(2)
#define DW_MCI_IDMAC_FS         BIT(3)
#define DW_MCI_IDMAC_OWN        BIT(31)

#define DW_MCI_DESC_ADDR        0x80010000
#define DW_MCI_DATA_ADDR        0x80011000
#define DW_MCI_READ_ADDR        0x80012000
#define DW_MCI_BLOCK_SIZE       512
#define DW_MCI_IMAGE_SIZE       (1 * MiB)

static QTestState *dw_mci_start(const char *sd_path)
{
    if (sd_path) {
        return qtest_initf("-machine phytium-pi -m 1G -display none "
                           "-drive file=%s,format=raw,if=sd,index=1",
                           sd_path);
    }

    return qtest_init("-machine phytium-pi -m 1G -display none");
}

static char *dw_mci_create_image(void)
{
    g_autoptr(GError) error = NULL;
    char *path = NULL;
    int fd;
    int ret;

    fd = g_file_open_tmp("dw-mci-XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, DW_MCI_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    return path;
}

static uint32_t dw_mci_command(QTestState *qts, uint32_t index,
                               uint32_t argument, uint32_t flags)
{
    uint32_t raw;

    qtest_writel(qts, DW_MCI_BASE + DW_MCI_RINTSTS, UINT32_MAX);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_CMDARG, argument);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_CMD,
                 DW_MCI_CMD_START | flags | index);
    raw = qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS);
    g_assert_cmphex(raw & DW_MCI_INT_CMD_DONE, ==, DW_MCI_INT_CMD_DONE);
    g_assert_cmphex(raw & DW_MCI_INT_RTO, ==, 0);

    return qtest_readl(qts, DW_MCI_BASE + DW_MCI_RESP0);
}

static void dw_mci_init_card(QTestState *qts)
{
    uint32_t rca;

    qtest_writel(qts, DW_MCI_BASE + DW_MCI_PWREN, 1);
    dw_mci_command(qts, 0, 0, 0);
    dw_mci_command(qts, 55, 0, DW_MCI_CMD_RESP_EXP);
    dw_mci_command(qts, 41, 0x41200000, DW_MCI_CMD_RESP_EXP);
    dw_mci_command(qts, 2, 0,
                   DW_MCI_CMD_RESP_EXP | DW_MCI_CMD_RESP_LONG);
    rca = dw_mci_command(qts, 3, 0, DW_MCI_CMD_RESP_EXP) & 0xffff0000;
    g_assert_cmphex(rca, !=, 0);
    dw_mci_command(qts, 7, rca, DW_MCI_CMD_RESP_EXP);
    dw_mci_command(qts, 16, DW_MCI_BLOCK_SIZE, DW_MCI_CMD_RESP_EXP);
}

static void dw_mci_write_descriptor(QTestState *qts, uint32_t control,
                                    uint32_t buffer)
{
    qtest_writel(qts, DW_MCI_DESC_ADDR, control);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 4, 0);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 8, DW_MCI_BLOCK_SIZE);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 12, 0);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 16, buffer);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 20, 0);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 24, 0);
    qtest_writel(qts, DW_MCI_DESC_ADDR + 28, 0);
}

static void dw_mci_prepare_idmac(QTestState *qts, uint32_t interrupts)
{
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_CTRL,
                 DW_MCI_CTRL_USE_IDMAC);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_BMOD,
                 DW_MCI_BMOD_ENABLE);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_BLKSIZ,
                 DW_MCI_BLOCK_SIZE);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_BYTCNT,
                 DW_MCI_BLOCK_SIZE);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_DBADDRL,
                 DW_MCI_DESC_ADDR);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_DBADDRU, 0);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_IDINTEN, interrupts);
}

static void test_registers_and_irq(void)
{
    QTestState *qts = dw_mci_start(NULL);

    qtest_irq_intercept_out_named(qts, "/machine/mci1", "sysbus-irq");

    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_CTRL), ==,
                    BIT(24));
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_STATUS) &
                    (BIT(1) | BIT(2)), ==, BIT(1) | BIT(2));
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_TMOUT), ==,
                    0xffffff40);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_BLKSIZ), ==,
                    DW_MCI_BLOCK_SIZE);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_BYTCNT), ==,
                    DW_MCI_BLOCK_SIZE);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_FIFOTH), ==,
                    0x01ff0000);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_CDETECT), ==, 1);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DEBNCE), ==,
                    0x00ffffff);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_VERID), ==,
                    0x280a);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_HCON), ==,
                    0x08000080);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RST_N), ==, 1);

    /* Exercise the 64-bit IDMAC layout selected by HCON.ADDR_CONFIG */
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_DBADDRL,
                 DW_MCI_DESC_ADDR + 3);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DBADDRL), ==,
                    DW_MCI_DESC_ADDR);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_DBADDRU, 1);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DBADDRU), ==, 1);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_IDINTEN,
                 DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_IDINTEN), ==,
                    DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DSCADDRU), ==, 0);

    qtest_writel(qts, DW_MCI_BASE + DW_MCI_RINTSTS, UINT32_MAX);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_CMD,
                 DW_MCI_CMD_START | DW_MCI_CMD_UPD_CLK);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS) &
                    DW_MCI_INT_CMD_DONE, ==, 0);

    qtest_writel(qts, DW_MCI_BASE + DW_MCI_CTRL,
                 DW_MCI_CTRL_INT_ENABLE);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_INTMASK,
                 DW_MCI_INT_CMD_DONE | DW_MCI_INT_RTO);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_CMD,
                 DW_MCI_CMD_START | DW_MCI_CMD_RESP_EXP);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_MINTSTS), ==,
                    DW_MCI_INT_CMD_DONE | DW_MCI_INT_RTO);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_RINTSTS,
                 DW_MCI_INT_CMD_DONE | DW_MCI_INT_RTO);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_CTRL), ==,
                    BIT(24));
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_BMOD), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DBADDRL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DBADDRU), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_IDINTEN), ==, 0);
    qtest_quit(qts);
}

static void test_idmac_write_and_read(void)
{
    g_autofree char *sd_path = dw_mci_create_image();
    uint8_t pattern[DW_MCI_BLOCK_SIZE];
    uint8_t actual[DW_MCI_BLOCK_SIZE];
    uint8_t zero[DW_MCI_BLOCK_SIZE] = { 0 };
    QTestState *qts = dw_mci_start(sd_path);
    uint32_t status;
    size_t i;
    ssize_t size;
    int fd;

    qtest_irq_intercept_out_named(qts, "/machine/mci1", "sysbus-irq");
    dw_mci_init_card(qts);

    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = i ^ 0xa5;
    }
    qtest_memwrite(qts, DW_MCI_DATA_ADDR, pattern, sizeof(pattern));
    dw_mci_write_descriptor(qts,
                            DW_MCI_IDMAC_OWN | DW_MCI_IDMAC_FS |
                            DW_MCI_IDMAC_LD,
                            DW_MCI_DATA_ADDR);
    dw_mci_prepare_idmac(qts, DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI);
    dw_mci_command(qts, 24, 0,
                   DW_MCI_CMD_RESP_EXP | DW_MCI_CMD_DAT_EXP |
                   DW_MCI_CMD_DAT_WR);

    g_assert_cmphex(qtest_readl(qts, DW_MCI_DESC_ADDR) &
                    DW_MCI_IDMAC_OWN, ==, 0);
    status = qtest_readl(qts, DW_MCI_BASE + DW_MCI_IDSTS);
    g_assert_cmphex(status & (DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI), ==,
                    DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DSCADDRL), ==,
                    DW_MCI_DESC_ADDR);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_DSCADDRU), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_BUFADDRL), ==,
                    DW_MCI_DATA_ADDR);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_BUFADDRU), ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS) &
                    DW_MCI_INT_DATA_OVER, ==, DW_MCI_INT_DATA_OVER);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_IDSTS,
                 DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_memwrite(qts, DW_MCI_READ_ADDR, zero, sizeof(zero));
    dw_mci_write_descriptor(qts,
                            DW_MCI_IDMAC_OWN | DW_MCI_IDMAC_FS |
                            DW_MCI_IDMAC_LD,
                            DW_MCI_READ_ADDR);
    dw_mci_prepare_idmac(qts, DW_MCI_IDSTS_RI | DW_MCI_IDSTS_NI);
    dw_mci_command(qts, 17, 0,
                   DW_MCI_CMD_RESP_EXP | DW_MCI_CMD_DAT_EXP);
    qtest_memread(qts, DW_MCI_READ_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), pattern, sizeof(pattern));
    status = qtest_readl(qts, DW_MCI_BASE + DW_MCI_IDSTS);
    g_assert_cmphex(status & (DW_MCI_IDSTS_RI | DW_MCI_IDSTS_NI), ==,
                    DW_MCI_IDSTS_RI | DW_MCI_IDSTS_NI);
    qtest_quit(qts);

    fd = open(sd_path, O_RDONLY);
    g_assert_cmpint(fd, >=, 0);
    size = read(fd, actual, sizeof(actual));
    close(fd);
    g_assert_cmpint(size, ==, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), pattern, sizeof(pattern));
    unlink(sd_path);
}

static void test_idmac_suspend_and_resume(void)
{
    g_autofree char *sd_path = dw_mci_create_image();
    uint8_t pattern[DW_MCI_BLOCK_SIZE];
    QTestState *qts = dw_mci_start(sd_path);
    uint32_t status;
    size_t i;

    qtest_irq_intercept_out_named(qts, "/machine/mci1", "sysbus-irq");
    dw_mci_init_card(qts);

    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = i ^ 0x69;
    }
    qtest_memwrite(qts, DW_MCI_DATA_ADDR, pattern, sizeof(pattern));
    dw_mci_write_descriptor(qts, 0, DW_MCI_DATA_ADDR);
    dw_mci_prepare_idmac(qts,
                         DW_MCI_IDSTS_TI | DW_MCI_IDSTS_DU |
                         DW_MCI_IDSTS_NI | DW_MCI_IDSTS_AI);
    dw_mci_command(qts, 24, 0,
                   DW_MCI_CMD_RESP_EXP | DW_MCI_CMD_DAT_EXP |
                   DW_MCI_CMD_DAT_WR);

    status = qtest_readl(qts, DW_MCI_BASE + DW_MCI_IDSTS);
    g_assert_cmphex(status & (DW_MCI_IDSTS_DU | DW_MCI_IDSTS_AI), ==,
                    DW_MCI_IDSTS_DU | DW_MCI_IDSTS_AI);
    g_assert_cmphex((status >> 13) & 0xf, ==, 1);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS) &
                    DW_MCI_INT_DATA_OVER, ==, 0);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, DW_MCI_BASE + DW_MCI_IDSTS,
                 DW_MCI_IDSTS_DU | DW_MCI_IDSTS_AI);
    g_assert_false(qtest_get_irq(qts, 0));
    dw_mci_write_descriptor(qts,
                            DW_MCI_IDMAC_OWN | DW_MCI_IDMAC_FS |
                            DW_MCI_IDMAC_LD,
                            DW_MCI_DATA_ADDR);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_PLDMND, 0);

    status = qtest_readl(qts, DW_MCI_BASE + DW_MCI_IDSTS);
    g_assert_cmphex(status & (DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI), ==,
                    DW_MCI_IDSTS_TI | DW_MCI_IDSTS_NI);
    g_assert_cmphex((status >> 13) & 0xf, ==, 0);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS) &
                    DW_MCI_INT_DATA_OVER, ==, DW_MCI_INT_DATA_OVER);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_fifo_data_port(void)
{
    g_autofree char *sd_path = dw_mci_create_image();
    uint8_t pattern[DW_MCI_BLOCK_SIZE];
    uint8_t actual[DW_MCI_BLOCK_SIZE];
    QTestState *qts = dw_mci_start(sd_path);
    size_t i;
    ssize_t size;
    int fd;

    dw_mci_init_card(qts);
    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = i ^ 0x3c;
    }

    qtest_writel(qts, DW_MCI_BASE + DW_MCI_BLKSIZ, DW_MCI_BLOCK_SIZE);
    qtest_writel(qts, DW_MCI_BASE + DW_MCI_BYTCNT, DW_MCI_BLOCK_SIZE);
    dw_mci_command(qts, 24, 0,
                   DW_MCI_CMD_RESP_EXP | DW_MCI_CMD_DAT_EXP |
                   DW_MCI_CMD_DAT_WR);
    for (i = 0; i < sizeof(pattern); i += sizeof(uint32_t)) {
        qtest_writel(qts, DW_MCI_BASE + DW_MCI_FIFO + (i % 64),
                     ldl_le_p(pattern + i));
    }
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_TCBCNT), ==,
                    DW_MCI_BLOCK_SIZE);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_TBBCNT), ==,
                    DW_MCI_BLOCK_SIZE);
    g_assert_cmphex(qtest_readl(qts, DW_MCI_BASE + DW_MCI_RINTSTS) &
                    DW_MCI_INT_DATA_OVER, ==, DW_MCI_INT_DATA_OVER);
    qtest_quit(qts);

    fd = open(sd_path, O_RDONLY);
    g_assert_cmpint(fd, >=, 0);
    size = read(fd, actual, sizeof(actual));
    close(fd);
    g_assert_cmpint(size, ==, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), pattern, sizeof(pattern));
    unlink(sd_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("dw-mci/registers-and-irq", test_registers_and_irq);
    qtest_add_func("dw-mci/idmac-write-and-read", test_idmac_write_and_read);
    qtest_add_func("dw-mci/idmac-suspend-and-resume",
                   test_idmac_suspend_and_resume);
    qtest_add_func("dw-mci/fifo-data-port", test_fifo_data_port);

    return g_test_run();
}
