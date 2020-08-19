/*
 * QTest testcase for fuzz case
 *
 * Copyright (c) 2020 Li Qiang <liq3ea@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"

#include "libqtest.h"

/*
 * This used to trigger the assert in scsi_dma_complete
 * https://bugs.launchpad.net/qemu/+bug/1878263
 */
static void test_megasas_zero_iov_cnt(void)
{
    QTestState *s;

    s = qtest_init("-nographic -monitor none -serial none "
                   "-M q35 -device megasas -device scsi-cd,drive=null0 "
                   "-blockdev driver=null-co,read-zeroes=on,node-name=null0");
    qtest_outl(s, 0xcf8, 0x80001818);
    qtest_outl(s, 0xcfc, 0xc101);
    qtest_outl(s, 0xcf8, 0x8000181c);
    qtest_outl(s, 0xcf8, 0x80001804);
    qtest_outw(s, 0xcfc, 0x7);
    qtest_outl(s, 0xcf8, 0x8000186a);
    qtest_writeb(s, 0x14, 0xfe);
    qtest_writeb(s, 0x0, 0x02);
    qtest_outb(s, 0xc1c0, 0x17);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("fuzz/megasas_zero_iov_cnt", test_megasas_zero_iov_cnt);

    return g_test_run();
}
