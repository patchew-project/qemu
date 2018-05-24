/*
 * QTest testcase for the Intel Hexadecimal Object File Loader
 *
 * Authors:
 *  Su Hang <suhang16@mails.ucas.ac.cn> 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define BIN_SIZE 146

static unsigned char pre_store[BIN_SIZE] = {
    4,   208, 159, 229, 22,  0,   0,   235, 254, 255, 255, 234, 152, 16,  1,
    0,   4,   176, 45,  229, 0,   176, 141, 226, 12,  208, 77,  226, 8,   0,
    11,  229, 6,   0,   0,   234, 8,   48,  27,  229, 0,   32,  211, 229, 44,
    48,  159, 229, 0,   32,  131, 229, 8,   48,  27,  229, 1,   48,  131, 226,
    8,   48,  11,  229, 8,   48,  27,  229, 0,   48,  211, 229, 0,   0,   83,
    227, 244, 255, 255, 26,  0,   0,   160, 225, 0,   208, 139, 226, 4,   176,
    157, 228, 30,  255, 47,  225, 0,   16,  31,  16,  0,   72,  45,  233, 4,
    176, 141, 226, 8,   0,   159, 229, 230, 255, 255, 235, 0,   0,   160, 225,
    0,   136, 189, 232, 132, 0,   1,   0,   0,   16,  31,  16,  72,  101, 108,
    108, 111, 32,  119, 111, 114, 108, 100, 33,  10,  0};

/* success if no crash or abort */
static void hex_loader_test(void)
{
    unsigned int i;
    unsigned char memory_content[BIN_SIZE];
    const unsigned int base_addr = 0x00010000;

    QTestState *s = qtest_startf(
        "-M versatilepb -m 128M -nographic -kernel ./tests/hex-loader-check-data/test.hex");

    for (i = 0; i < BIN_SIZE; ++i) {
        memory_content[i] = qtest_readb(s, base_addr + i);
        g_assert_cmpuint(memory_content[i], ==, pre_store[i]);
    }
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/tmp/hex_loader", hex_loader_test);
    ret = g_test_run();

    return ret;
}
