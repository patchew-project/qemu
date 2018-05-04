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

static void sd_prepare_request48(SDFrame48 *frame, uint8_t cmd, uint32_t arg)
{
    sd_prepare_request(frame, cmd, arg, /* gen_crc */ true);
}

static void sd_prepare_response48(SDFrame48 *frame, uint8_t cmd, uint32_t arg)
{
    sd_prepare_frame48(frame, cmd, arg, /* is_resp */ true, /* gen_crc */ true);
}

static void test_sd_request_frame_crc7(void)
{
    SDFrame48 frame;

    /* CMD0 */
    sd_prepare_request48(&frame, 0, 0);
    g_assert_cmphex(frame.crc, ==, 0b1001010);

    /* CMD17 */
    sd_prepare_request48(&frame, 17, 0);
    g_assert_cmphex(frame.crc, ==, 0b0101010);

    /* APP_CMD */
    sd_prepare_request48(&frame, 55, 0);
    g_assert_cmphex(frame.crc, ==, 0x32);

    /* ACMD41 SEND_OP_COND */
    sd_prepare_request48(&frame, 41, 0x00100000);
    g_assert_cmphex(frame.crc, ==, 0x5f >> 1);

    /* CMD2 ALL_SEND_CID */
    sd_prepare_request48(&frame, 2, 0);
    g_assert_cmphex(frame.crc, ==, 0x4d >> 1);
}

static void test_sd_response_frame48_crc7(void)
{
    SDFrame48 frame;

    /* response to CMD17 */
    sd_prepare_response48(&frame, 17, 0x00000900);
    g_assert_cmphex(frame.crc, ==, 0b0110011);

    /* response to the APP_CMD */
    sd_prepare_response48(&frame, 55, 0x00000120);
    g_assert_cmphex(frame.crc, ==, 0x41);

    /* response to CMD3 SEND_RELATIVE_ADDR (Relative Card Address is 0xb368) */
    sd_prepare_response48(&frame, 3, 0xb3680500);
    g_assert_cmphex(frame.crc, ==, 0x0c);
}

static void test_sd_response_frame136_crc7(void)
{
    SDFrame136 frame;

    /* response to CMD2 ALL_SEND_CID */
    memcpy(&frame.content,
           "\x1d\x41\x44\x53\x44\x20\x20\x20\x10\xa0\x40\x0b\xc1\x00\x88",
           sizeof(frame.content));
    sd_update_frame136_checksum(&frame);
    g_assert_cmphex(frame.crc, ==, 0xad);
}

static void test_sd_data_frame_crc16(void)
{
    SDFrameData frame;

    memset(frame.content, 0xff, sizeof(frame.content));
    sd_update_framedata_checksum(&frame);
    g_assert_cmphex(frame.crc, ==, 0x7fa1);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("sd/req_crc7", test_sd_request_frame_crc7);
    qtest_add_func("sd/resp48_crc7", test_sd_response_frame48_crc7);
    qtest_add_func("sd/resp136_crc7", test_sd_response_frame136_crc7);
    qtest_add_func("sd/data_crc16", test_sd_data_frame_crc16);

    return g_test_run();
}
