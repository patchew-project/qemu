/*
 * QTest testcase for am53c974
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"


static void test_cmdfifo_underflow_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xcf8, 0x8000100e);
    qtest_outl(s, 0xcfc, 0x8a000000);
    qtest_outl(s, 0x8a09, 0x42000000);
    qtest_outl(s, 0x8a0d, 0x00);
    qtest_outl(s, 0x8a0b, 0x1000);
    qtest_quit(s);
}

static void test_cmdfifo_overflow_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xcf8, 0x8000100e);
    qtest_outl(s, 0xcfc, 0x0e000000);
    qtest_outl(s, 0xe40, 0x03);
    qtest_outl(s, 0xe0b, 0x4100);
    qtest_outl(s, 0xe0b, 0x9000);
    qtest_quit(s);
}

static void test_target_selected_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001001);
    qtest_outl(s, 0xcfc, 0x01000000);
    qtest_outl(s, 0xcf8, 0x8000100e);
    qtest_outl(s, 0xcfc, 0xef800000);
    qtest_outl(s, 0xef8b, 0x4100);
    qtest_outw(s, 0xef80, 0x01);
    qtest_outl(s, 0xefc0, 0x03);
    qtest_outl(s, 0xef8b, 0xc100);
    qtest_outl(s, 0xef8b, 0x9000);
    qtest_quit(s);
}

static void test_fifo_underflow_on_write_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xc008, 0x0a);
    qtest_outl(s, 0xc009, 0x41000000);
    qtest_outl(s, 0xc009, 0x41000000);
    qtest_outl(s, 0xc00b, 0x1000);
    qtest_quit(s);
}

static void test_cancelled_request_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x05);
    qtest_outb(s, 0xc046, 0x02);
    qtest_outl(s, 0xc00b, 0xc100);
    qtest_outl(s, 0xc040, 0x03);
    qtest_outl(s, 0xc040, 0x03);
    qtest_bufwrite(s, 0x0, "\x41", 0x1);
    qtest_outl(s, 0xc00b, 0xc100);
    qtest_outw(s, 0xc040, 0x02);
    qtest_outw(s, 0xc040, 0x81);
    qtest_outl(s, 0xc00b, 0x9000);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0) {
        qtest_add_func("am53c974/test_cmdfifo_underflow_ok",
                       test_cmdfifo_underflow_ok);
        qtest_add_func("am53c974/test_cmdfifo_overflow_ok",
                       test_cmdfifo_overflow_ok);
        qtest_add_func("am53c974/test_target_selected_ok",
                       test_target_selected_ok);
        qtest_add_func("am53c974/test_fifo_underflow_on_write_ok",
                       test_fifo_underflow_on_write_ok);
        qtest_add_func("am53c974/test_cancelled_request_ok",
                       test_cancelled_request_ok);
    }

    return g_test_run();
}
