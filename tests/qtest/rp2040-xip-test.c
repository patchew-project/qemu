/*
 * QTest testcase for the RP2040 XIP/SSI block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"

#define XIP_BASE     0x10000000
#define XIP_NOCACHE_NOALLOC_BASE 0x13000000
#define XIP_CTRL_BASE 0x14000000
#define XIP_STAT     0x08
#define XIP_STREAM_ADDR 0x14
#define XIP_STREAM_CTR  0x18
#define XIP_STREAM_FIFO 0x1c
#define XIP_SSI_BASE 0x18000000
#define SSI_CTRLR1   0x04
#define SSI_SSIENR   0x08
#define SSI_DR0      0x60
#define SSI_SER      0x10

#define IOQSPI_BASE    0x40018000
#define IOQSPI_SS_CTRL 0x0c
#define IOQSPI_OUT_LOW  0x200
#define IOQSPI_OUT_HIGH 0x300

#define XIP_STAT_FIFO_FULL  BIT(2)
#define XIP_STAT_FIFO_EMPTY BIT(1)
#define XIP_STAT_FLUSH_READY BIT(0)

#define UF2_BLOCK_SIZE          512
#define UF2_PAYLOAD_OFFSET      32
#define UF2_PAYLOAD_SIZE        256
#define UF2_MAGIC_START0        0x0a324655
#define UF2_MAGIC_START1        0x9e5d5157
#define UF2_MAGIC_END           0x0ab16f30
#define UF2_FLAG_FAMILY_ID      0x00002000
#define UF2_RP2040_FAMILY_ID    0xe48bff56

static QTestState *rp2040_start(const char *machine_args)
{
    if (machine_args) {
        return qtest_initf("-machine raspi-pico,%s", machine_args);
    }
    return qtest_init("-machine raspi-pico");
}

static void build_uf2_block(uint8_t *block, uint32_t target, uint32_t index,
                            const uint8_t *payload)
{
    memset(block, 0, UF2_BLOCK_SIZE);
    stl_le_p(block, UF2_MAGIC_START0);
    stl_le_p(block + 4, UF2_MAGIC_START1);
    stl_le_p(block + 8, UF2_FLAG_FAMILY_ID);
    stl_le_p(block + 12, target);
    stl_le_p(block + 16, UF2_PAYLOAD_SIZE);
    stl_le_p(block + 20, index);
    stl_le_p(block + 24, 2);
    stl_le_p(block + 28, UF2_RP2040_FAMILY_ID);
    memcpy(block + UF2_PAYLOAD_OFFSET, payload, UF2_PAYLOAD_SIZE);
    stl_le_p(block + UF2_BLOCK_SIZE - 4, UF2_MAGIC_END);
}

static void test_uf2_kernel_load(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *path = NULL;
    uint8_t uf2[2 * UF2_BLOCK_SIZE];
    uint8_t boot2[UF2_PAYLOAD_SIZE];
    uint8_t payload[UF2_PAYLOAD_SIZE];
    uint8_t actual[UF2_PAYLOAD_SIZE];
    QTestState *qts;
    int fd;
    size_t i;

    memset(boot2, 0x5a, sizeof(boot2));
    for (i = 0; i < ARRAY_SIZE(payload); i++) {
        payload[i] = i ^ 0xa5;
    }
    build_uf2_block(uf2, XIP_BASE, 0, boot2);
    build_uf2_block(uf2 + UF2_BLOCK_SIZE, XIP_BASE + 0x2000, 1, payload);

    fd = g_file_open_tmp("qemu-rp2040-uf2-XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)uf2, sizeof(uf2),
                                      &error));
    g_assert_no_error(error);

    qts = qtest_initf("-machine raspi-pico -kernel %s", path);
    qtest_memread(qts, XIP_BASE + 0x2000, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), payload, sizeof(payload));
    qtest_quit(qts);

    unlink(path);
}

static void read_flash_uid(QTestState *qts, uint8_t *uid)
{
    int i;

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x4b);
    qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
    for (i = 0; i < 4; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0);
        qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
    }
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0);
        uid[i] = qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
    }
}

static void program_flash_bytes(QTestState *qts, uint32_t off,
                                const uint8_t *buf, size_t len)
{
    size_t i;

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x06);

    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x02);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 16, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 8, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 0, 8));
    for (i = 0; i < len; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, buf[i]);
    }
    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 0);

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x05);
    qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
}

static void program_flash_bytes_without_wel(QTestState *qts, uint32_t off,
                                            const uint8_t *buf, size_t len)
{
    size_t i;

    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x02);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 16, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 8, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 0, 8));
    for (i = 0; i < len; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, buf[i]);
    }
    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 0);
}

static void erase_flash_sector(QTestState *qts, uint32_t off)
{
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x06);
    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x20);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 16, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 8, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 0, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 0);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x05);
    qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
}

static void test_flash_uid_default(void)
{
    static const uint8_t expected[] = {
        0x3e, 0xb8, 0xa7, 0x49, 0x3f, 0xcc, 0x06, 0x08,
    };
    QTestState *qts = rp2040_start(NULL);
    uint8_t uid[8];

    read_flash_uid(qts, uid);
    g_assert_cmpmem(uid, sizeof(uid), expected, sizeof(expected));

    qtest_quit(qts);
}

static void test_flash_uid_machine_option(void)
{
    static const uint8_t expected[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    };
    QTestState *qts = rp2040_start("flash-uid=0011223344556677");
    uint8_t uid[8];

    read_flash_uid(qts, uid);
    g_assert_cmpmem(uid, sizeof(uid), expected, sizeof(expected));

    qtest_quit(qts);
}

static void test_flash_program_erase(void)
{
    static const uint8_t first[] = { 0x0f, 0x55, 0xaa, 0xf0 };
    static const uint8_t second[] = { 0xf0, 0xaa, 0x55, 0x0f };
    static const uint8_t combined[] = { 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t erased[] = { 0xff, 0xff, 0xff, 0xff };
    uint8_t actual[sizeof(first)];
    QTestState *qts = rp2040_start(NULL);

    program_flash_bytes_without_wel(qts, 0x1000, first, sizeof(first));
    qtest_memread(qts, XIP_BASE + 0x1000, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), erased, sizeof(erased));

    program_flash_bytes(qts, 0x1000, first, sizeof(first));
    program_flash_bytes(qts, 0x1000, second, sizeof(second));
    qtest_memread(qts, XIP_BASE + 0x1000, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), combined, sizeof(combined));

    erase_flash_sector(qts, 0x1000);
    qtest_memread(qts, XIP_BASE + 0x1000, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), erased, sizeof(erased));

    qtest_quit(qts);
}

static void test_flash_persistence(void)
{
    static const uint8_t expected[] = { 0x12, 0x34, 0x56, 0x78 };
    g_autoptr(GError) error = NULL;
    g_autofree char *path = NULL;
    g_autofree char *machine_args = NULL;
    uint8_t initial[4096];
    uint8_t actual[sizeof(expected)];
    QTestState *qts;
    int fd;

    memset(initial, 0xff, sizeof(initial));
    fd = g_file_open_tmp("qemu-rp2040-flash-XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)initial,
                                      sizeof(initial), &error));
    g_assert_no_error(error);

    machine_args = g_strdup_printf("flash-file=%s", path);
    qts = rp2040_start(machine_args);
    program_flash_bytes(qts, 0x800, expected, sizeof(expected));
    qtest_quit(qts);

    qts = rp2040_start(machine_args);
    qtest_memread(qts, XIP_BASE + 0x800, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    qtest_quit(qts);

    unlink(path);
}

static void test_ioqspi_chip_select(void)
{
    static const uint8_t expected[] = { 0x5a, 0xa5 };
    uint8_t actual[sizeof(expected)];
    QTestState *qts = rp2040_start(NULL);
    size_t i;

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x06);
    qtest_writel(qts, IOQSPI_BASE + IOQSPI_SS_CTRL, IOQSPI_OUT_LOW);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x02);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x00);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x20);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x00);
    for (i = 0; i < ARRAY_SIZE(expected); i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, expected[i]);
    }
    qtest_writel(qts, IOQSPI_BASE + IOQSPI_SS_CTRL, IOQSPI_OUT_HIGH);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x05);
    qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);

    qtest_memread(qts, XIP_BASE + 0x2000, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    qtest_quit(qts);
}

static void test_stream_fifo(void)
{
    static const uint32_t expected[] = {
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514,
    };
    QTestState *qts = rp2040_start(NULL);
    size_t i;

    program_flash_bytes(qts, 0, (const uint8_t *)expected, sizeof(expected));

    qtest_writel(qts, XIP_CTRL_BASE + XIP_STREAM_ADDR, XIP_BASE);
    qtest_writel(qts, XIP_CTRL_BASE + XIP_STREAM_CTR, ARRAY_SIZE(expected));

    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STAT) &
                    (XIP_STAT_FLUSH_READY | XIP_STAT_FIFO_FULL), ==,
                    XIP_STAT_FLUSH_READY | XIP_STAT_FIFO_FULL);
    for (i = 0; i < ARRAY_SIZE(expected); i++) {
        g_assert_cmphex(qtest_readl(qts,
                                   XIP_CTRL_BASE + XIP_STREAM_FIFO), ==,
                        expected[i]);
    }
    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STREAM_CTR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STAT) &
                    XIP_STAT_FIFO_EMPTY, ==, XIP_STAT_FIFO_EMPTY);

    qtest_quit(qts);
}

static void test_ssi_bulk_read(void)
{
    static const uint32_t expected[] = {
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514,
    };
    QTestState *qts = rp2040_start(NULL);
    size_t i;

    program_flash_bytes(qts, 0, (const uint8_t *)expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(qts, XIP_NOCACHE_NOALLOC_BASE), ==,
                    expected[0]);

    qtest_writel(qts, XIP_SSI_BASE + SSI_SSIENR, 0);
    qtest_writel(qts, XIP_SSI_BASE + SSI_CTRLR1, ARRAY_SIZE(expected) - 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_SSIENR, 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0xa0);

    for (i = 0; i < ARRAY_SIZE(expected); i++) {
        g_assert_cmphex(bswap32(qtest_readl(qts,
                                           XIP_SSI_BASE + SSI_DR0)), ==,
                        expected[i]);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-xip/flash-uid-default",
                   test_flash_uid_default);
    qtest_add_func("/rp2040-xip/flash-uid-machine-option",
                   test_flash_uid_machine_option);
    qtest_add_func("/rp2040-xip/uf2-kernel-load", test_uf2_kernel_load);
    qtest_add_func("/rp2040-xip/flash-program-erase",
                   test_flash_program_erase);
    qtest_add_func("/rp2040-xip/flash-persistence",
                   test_flash_persistence);
    qtest_add_func("/rp2040-xip/ioqspi-chip-select",
                   test_ioqspi_chip_select);
    qtest_add_func("/rp2040-xip/stream-fifo", test_stream_fifo);
    qtest_add_func("/rp2040-xip/ssi-bulk-read", test_ssi_bulk_read);

    return g_test_run();
}
