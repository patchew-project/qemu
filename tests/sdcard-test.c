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

static void sd_prepare_request48(uint8_t *buf, size_t bufsize,
                                 uint8_t cmd, uint32_t arg)
{
    sd_frame48_init(buf, bufsize, cmd, arg, /* is_resp */ false);
    buf[5] = sd_frame48_calc_checksum(buf);
}

static void sd_prepare_response48(uint8_t *buf, size_t bufsize,
                                  uint8_t cmd, uint32_t arg)
{
    sd_frame48_init(buf, bufsize, cmd, arg, /* is_resp */ true);
    buf[5] = sd_frame48_calc_checksum(buf);
}

static void test_sd_request_frame_crc7(void)
{
    uint8_t req[6];

    /* CMD0 */
    sd_prepare_request48(req, sizeof(req), 0, 0);
    g_assert_cmphex(req[5], ==, 0b1001010);

    /* CMD17 */
    sd_prepare_request48(req, sizeof(req), 17, 0);
    g_assert_cmphex(req[5], ==, 0b0101010);

    /* APP_CMD */
    sd_prepare_request48(req, sizeof(req), 55, 0);
    g_assert_cmphex(req[5], ==, 0x32);

    /* ACMD41 SEND_OP_COND */
    sd_prepare_request48(req, sizeof(req), 41, 0x00100000);
    g_assert_cmphex(req[5], ==, 0x5f >> 1);

    /* CMD2 ALL_SEND_CID */
    sd_prepare_request48(req, sizeof(req), 2, 0);
    g_assert_cmphex(req[5], ==, 0x4d >> 1);

    g_assert_true(sd_frame48_verify_checksum(req));
}

static void test_sd_response_frame48_crc7(void)
{
    uint8_t resp[6];

    /* response to CMD17 */
    sd_prepare_response48(resp, sizeof(resp), 17, 0x00000900);
    g_assert_cmphex(resp[5], ==, 0b0110011);

    /* response to the APP_CMD */
    sd_prepare_response48(resp, sizeof(resp), 55, 0x00000120);
    g_assert_cmphex(resp[5], ==, 0x41);

    /* response to CMD3 SEND_RELATIVE_ADDR (Relative Card Address is 0xb368) */
    sd_prepare_response48(resp, sizeof(resp), 3, 0xb3680500);
    g_assert_cmphex(resp[5], ==, 0x0c);

    g_assert_true(sd_frame48_verify_checksum(resp));
}

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

static void test_sd_verify_cksum_frame48(void)
{
    uint8_t buf[6];

    sd_prepare_request48(buf, sizeof(buf), 42, 0x12345678);
    g_assert_true(sd_frame48_verify_checksum(buf));

    sd_prepare_response48(buf, sizeof(buf), 69, 0x98765432);
    g_assert_true(sd_frame48_verify_checksum(buf));
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

    qtest_add_func("sd/prepare_req_crc7", test_sd_request_frame_crc7);
    qtest_add_func("sd/prepare_resp48_crc7", test_sd_response_frame48_crc7);
    qtest_add_func("sd/prepare_resp136_crc7", test_sd_response_frame136_crc7);
    qtest_add_func("sd/verify_cksum_frame48", test_sd_verify_cksum_frame48);
    qtest_add_func("sd/verify_cksum_frame136", test_sd_verify_cksum_frame136);

    return g_test_run();
}
