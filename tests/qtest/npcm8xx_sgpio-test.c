/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * QTest testcase for the Nuvoton NPCM8xx I/O EXPANSION INTERFACE (SOIX)
 * modules.
 *
 * Copyright 2025 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define NR_SGPIO_DEVICES 8
#define SGPIO(x)         (0xf0101000 + (x) * 0x1000)
#define SGPIO_IRQ(x)     (19 + (x))

/* SGPIO registers */
#define GP_N_XDOUT(x)   (0x00 + x)
#define GP_N_XDIN(x)    (0x08 + x)
#define GP_N_XEVCFG(x)  (0x10 + (x) * 0x2)
#define GP_N_XEVSTS(x)  (0x20 + x)
#define GP_N_IOXCTS     0x28
#define GP_N_IOXINDR    0x29
#define GP_N_IOXCFG1    0x2a
#define GP_N_IOXCFG2    0x2b
#define GP_N_RD_MODE_PERIODIC  0x4
#define GP_N_IOXIF_EN  0x80


/* Restore the SGPIO controller to a sensible default state. */
static void sgpio_reset(int n)
{
    int i;

    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        writel(SGPIO(n) + GP_N_XDOUT(i), 0x00000000);
        writel(SGPIO(n) + GP_N_XEVCFG(i), 0x00000000);
        writel(SGPIO(n) + GP_N_XEVSTS(i), 0x00000000);
    }
    writel(SGPIO(n) + GP_N_IOXCTS, 0x00000000);
    writel(SGPIO(n) + GP_N_IOXINDR, 0x00000000);
    writel(SGPIO(n) + GP_N_IOXCFG1, 0x00000000);
    writel(SGPIO(n) + GP_N_IOXCFG2, 0x00000000);
}

static void test_read_dout_byte(void)
{
    int i;

    sgpio_reset(0);

    /* set all 8 output devices */
    writel(SGPIO(0) + GP_N_IOXCFG2, NR_SGPIO_DEVICES << 4);
    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        writel(SGPIO(0) + GP_N_XDOUT(i), 0xff);
        g_assert_cmphex(readb(SGPIO(0) + GP_N_XDOUT(i)), ==, 0xff);
    }
}

static void test_read_dout_word(void)
{
    int i;

    sgpio_reset(0);
    /* set all 8 output devices */
    writel(SGPIO(0) + GP_N_IOXCFG2, NR_SGPIO_DEVICES << 4);
    /* set 16 bit aligned access */
    writel(SGPIO(0) + GP_N_IOXCTS, 1 << 3);
    for (i = 0; i < NR_SGPIO_DEVICES / 2; ++i) {
        writel(SGPIO(0) + GP_N_XDOUT(i * 2), 0xf0f0);
        g_assert_cmphex(readw(SGPIO(0) + GP_N_XDOUT(i * 2)), ==, 0xf0f0);
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("/npcm8xx_sgpio/read_dout_byte", test_read_dout_byte);
    qtest_add_func("/npcm8xx_sgpio/read_dout_word", test_read_dout_word);

    qtest_start("-machine npcm845-evb");
    qtest_irq_intercept_in(global_qtest, "/machine/soc/sgpio");
    ret = g_test_run();
    qtest_end();

    return ret;
}
