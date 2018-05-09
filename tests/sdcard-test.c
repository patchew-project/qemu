/*
 * QTest testcase for SD protocol and cards
 *
 * Examples taken from:
 *
 * - Physical Layer Simplified Specification (chap. 4.5: Cyclic Redundancy Code)
 * - http://wiki.seabright.co.nz/wiki/SdCardProtocol.html
 *
 * Tests written by Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/sd/sd.h"

static void test_sd_response_frame136_crc7(void)
{
    uint8_t buf[16];

    /* response to CMD2 ALL_SEND_CID */
    memcpy(&buf, "\x1d\x41\x44\x53\x44\x20\x20\x20\x10\xa0\x40\x0b\xc1\x00\x88",
           sizeof(buf));
    buf[15] = sd_frame136_calc_checksum(buf);
    g_assert_cmphex(buf[15], ==, 0xad);

    g_assert_true(sd_frame136_verify_checksum(buf));
}

static void test_sd_verify_cksum_frame136(void)
{
    uint8_t buf[16];

    memset(buf, 69, sizeof(buf));
    buf[15] = sd_frame136_calc_checksum(buf);
    g_assert_true(sd_frame136_verify_checksum(buf));
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("sd/prepare_resp136_crc7", test_sd_response_frame136_crc7);
    qtest_add_func("sd/verify_cksum_frame136", test_sd_verify_cksum_frame136);

    return g_test_run();
}
