/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest for the RER/WER (external register) instructions of the dc233c core.
 *
 * The core's ISA tables used to describe RER and WER without operands, so
 * translating them read uninitialised operands and crashed QEMU.  The guest
 * executes RER then WER on an unmapped external register address, then
 * writes a marker to RAM, which the test polls.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define MARKER_ADDR 0x100
#define MARKER_VAL  0x123

/*
 * Guest ELF, assembled with xtensa-dc233c-elf-gcc (whose assembler has the
 * same operand-less RER/WER, hence the raw bytes) at the reset vector
 * 0xfe000000, then reduced to one PT_LOAD segment:
 *
 *   _start:
 *     movi.n a2, 0x40
 *     .byte 0x30, 0x62, 0x40      # rer a3, a2
 *     .byte 0x30, 0x72, 0x40      # wer a3, a2
 *     movi   a4, 0x100
 *     movi   a5, 0x123
 *     s32i.n a5, a4, 0
 *   1: j 1b
 */
static const uint8_t xtensa_guest[] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x5e, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xfe, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0xfe, 0x14, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x4c, 0x02, 0x30, 0x62, 0x40, 0x30, 0x72, 0x40, 0x42, 0xa1, 0x00, 0x52,
    0xa1, 0x23, 0x59, 0x04, 0x06, 0xff, 0xff, 0x00,
};

static void test_extreg(void)
{
    g_autofree char *elf = NULL;
    QTestState *qts;
    int fd;

    fd = g_file_open_tmp("qtest-xtensa-extreg-XXXXXX", &elf, NULL);
    g_assert(fd != -1);
    g_assert_cmpint(write(fd, xtensa_guest, sizeof(xtensa_guest)), ==,
                    sizeof(xtensa_guest));
    close(fd);

    qts = qtest_initf("-M sim -cpu dc233c -accel tcg -no-shutdown "
                      "-kernel %s", elf);
    unlink(elf);

    /* The harness enforces a timeout; stop if QEMU died */
    while (qtest_readl(qts, MARKER_ADDR) != MARKER_VAL
           && qtest_probe_child(qts)) {
        g_usleep(10000);
    }
    g_assert_cmpint(qtest_readl(qts, MARKER_ADDR), ==, MARKER_VAL);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_accel("tcg")) {
        g_test_skip("TCG not available");
        return g_test_run();
    }
    qtest_add_func("xtensa/extreg", test_extreg);

    return g_test_run();
}
