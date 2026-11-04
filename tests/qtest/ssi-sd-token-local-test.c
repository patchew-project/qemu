/*
 * SD/SPI transfert tests
 *
 * Copyright (c) 2026 Manuele Conti <manuele.conti@archimede-energia.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define SSP_BASE 0x40008000
#define GPIO_D_DATA0 0x40007004
#define GPIO_D_DIR 0x40007400
#define BLOCK_SIZE 512

typedef struct WriteCase {
    const char *name;
    bool multi;
    unsigned dummy_bytes;
} WriteCase;

static uint8_t transfer(uint8_t value)
{
    writel(SSP_BASE + 8, value);
    return readl(SSP_BASE + 8) & 0xff;
}

static uint8_t command(unsigned cmd, uint32_t argument)
{
    transfer(0xff);
    transfer(0x40 | cmd);
    transfer(argument >> 24);
    transfer(argument >> 16);
    transfer(argument >> 8);
    transfer(argument);
    transfer(cmd == 0 ? 0x95 : cmd == 8 ? 0x87 : 0xff);
    for (unsigned i = 0; i < 32; i++) {
        uint8_t response = transfer(0xff);
        if (!(response & 0x80)) {
            return response;
        }
    }
    g_assert_not_reached();
}

static void initialize_card(void)
{
    bool ready = false;
    bool token = false;

    /* Drive D0 high and then low to select SD and deselect the OLED. */
    writel(GPIO_D_DIR, 1);
    writel(GPIO_D_DATA0, 1);
    writel(GPIO_D_DATA0, 0);
    writel(SSP_BASE, 7);       /* Eight-bit SPI frames. */
    writel(SSP_BASE + 4, 2);   /* Enable PL022. */
    for (unsigned i = 0; i < 10; i++) {
        transfer(0xff);
    }
    g_assert_cmphex(command(0, 0), ==, 1);
    g_assert_cmphex(command(8, 0x1aa), ==, 1);
    for (unsigned i = 0; i < 4; i++) {
        transfer(0xff);
    }
    for (unsigned i = 0; i < 100; i++) {
        g_assert_cmphex(command(55, 0), ==, 1);
        if (command(41, 0x40000000) == 0) {
            ready = true;
            break;
        }
        clock_step(1000000);
    }
    g_assert_true(ready);

    /* Upstream's SPI CMD9 handler switches the card to transfer state. */
    g_assert_cmphex(command(9, 0), ==, 0);
    for (unsigned i = 0; i < 32; i++) {
        if (transfer(0xff) == 0xfe) {
            token = true;
            break;
        }
    }
    g_assert_true(token);
    for (unsigned i = 0; i < 18; i++) {
        transfer(0xff);       /* Sixteen CSD bytes and two CRC bytes. */
    }
    g_assert_cmphex(command(16, BLOCK_SIZE), ==, 0);
}

static void test_write(const void *opaque)
{
    const WriteCase *tc = opaque;
    unsigned blocks = tc->multi ? 2 : 1;
    uint8_t expected[2 * BLOCK_SIZE], actual[2 * BLOCK_SIZE];
    uint8_t response[2] = { 0xff, 0xff };
    bool accepted = true;
    g_autofree char *path = NULL;
    g_autofree char *args = NULL;
    int fd = g_file_open_tmp("ssi-sd-token-local-XXXXXX", &path, NULL);
    ssize_t bytes;

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 64 * 1024 * 1024), ==, 0);
    args = g_strdup_printf("-M lm3s6965evb -serial none "
                           "-drive file=%s,if=sd,format=raw", path);
    qtest_start(args);
    initialize_card();
    g_assert_cmphex(command(tc->multi ? 25 : 24, 0), ==, 0);
    for (unsigned i = 0; i < tc->dummy_bytes; i++) {
        transfer(0xff);
    }
    for (unsigned block = 0; block < blocks; block++) {
        /* Constant data avoids accidentally supplying a second start token. */
        memset(expected + block * BLOCK_SIZE, 0xa5 + block, BLOCK_SIZE);
        transfer(tc->multi ? 0xfc : 0xfe);
        for (unsigned i = 0; i < BLOCK_SIZE; i++) {
            transfer(expected[block * BLOCK_SIZE + i]);
        }
        transfer(0xff);
        transfer(0xff);
        response[block] = transfer(0xff);
        if ((response[block] & 0x1f) != 0x05) {
            accepted = false;
            break;
        }
        /* This experiment isolates the first token after command R1. */
        if (tc->multi && block + 1 < blocks) {
            transfer(0xff);
        }
    }
    if (tc->multi && accepted) {
        transfer(0xff);
        transfer(0xfd);
        transfer(0xff);
    }
    qtest_end();
    bytes = pread(fd, actual, blocks * BLOCK_SIZE, 0);
    close(fd);
    unlink(path);

    g_test_message("CMD%u dummy=%u response[0]=0x%02x response[1]=0x%02x",
                   tc->multi ? 25 : 24, tc->dummy_bytes,
                   response[0], response[1]);
    g_assert_true(accepted);
    g_assert_cmpint(bytes, ==, blocks * BLOCK_SIZE);
    g_assert_cmpmem(actual, blocks * BLOCK_SIZE,
                   expected, blocks * BLOCK_SIZE);
}

int main(int argc, char **argv)
{
    static const WriteCase cases[] = {
        { "/ssi-sd-local/single/immediate", false, 0 },
        { "/ssi-sd-local/single/dummy", false, 1 },
        { "/ssi-sd-local/multi/immediate", true, 0 },
        { "/ssi-sd-local/multi/dummy", true, 1 },
    };

    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(cases); i++) {
        qtest_add_data_func(cases[i].name, &cases[i], test_write);
    }
    return g_test_run();
}
