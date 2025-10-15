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


static void qtest_qom_set_uint64(QTestState *s, const char *path,
                                 const char *property, uint64_t value)
{
    QDict *r;
    QDict *qdict;

    r = qtest_qmp(s, "{ 'execute': 'qom-set', 'arguments': "
                     "{ 'path': %s, 'property': %s, 'value': %" PRIu64 " } }",
                     path, property, value);

    qdict = qdict_get_qdict(r, "error");
    if (qdict) {
        printf("DEBUG: set error: %s\n", qdict_get_try_str(qdict, "desc"));
    }

    qobject_unref(r);
}


static uint64_t qtest_qom_get_uint64(QTestState *s, const char *path,
                                     const char *property)
{
    QDict *r;

    uint64_t res;
    r = qtest_qmp(s, "{ 'execute': 'qom-get', 'arguments': "
                     "{ 'path': %s, 'property': %s } }", path, property);

    res = qdict_get_uint(r, "return");
    qobject_unref(r);

    return res;
}

/* Restore the SGPIO controller to a sensible default state. */
static void sgpio_reset(QTestState *s, int n)
{
    int i;

    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        qtest_writeq(s, SGPIO(n) + GP_N_XDOUT(i), 0x0);
        qtest_writeq(s, SGPIO(n) + GP_N_XEVCFG(i), 0x0);
        qtest_writeq(s, SGPIO(n) + GP_N_XEVSTS(i), 0x0);
    }
    qtest_writeq(s, SGPIO(n) + GP_N_IOXCTS, 0x0);
    qtest_writeq(s, SGPIO(n) + GP_N_IOXINDR, 0x0);
    qtest_writeq(s, SGPIO(n) + GP_N_IOXCFG1, 0x0);
    qtest_writeq(s, SGPIO(n) + GP_N_IOXCFG2, 0x0);
}

static void test_read_dout_byte(const char *machine)
{
    QTestState *s = qtest_init(machine);
    int i;

    sgpio_reset(s, 0);

    /* set all 8 output devices */
    qtest_writeq(s, SGPIO(0) + GP_N_IOXCFG2, NR_SGPIO_DEVICES << 4);
    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        qtest_writeq(s, SGPIO(0) + GP_N_XDOUT(i), 0xff);
        g_assert_cmphex(qtest_readb(s, SGPIO(0) + GP_N_XDOUT(i)), ==, 0xff);
    }
    qtest_quit(s);
}

static void test_read_dout_word(const char *machine)
{
    QTestState *s = qtest_init(machine);
    int i;

    sgpio_reset(s, 0);
    /* set all 8 output devices */
    qtest_writeq(s, SGPIO(0) + GP_N_IOXCFG2, NR_SGPIO_DEVICES << 4);
    /* set 16 bit aligned access */
    qtest_writeq(s, SGPIO(0) + GP_N_IOXCTS, 1 << 3);
    for (i = 0; i < NR_SGPIO_DEVICES / 2; ++i) {
        qtest_writeq(s, SGPIO(0) + GP_N_XDOUT(i * 2), 0xf0f0);
        g_assert_cmphex(qtest_readw(s, SGPIO(0) + GP_N_XDOUT(i * 2)),
                        ==, 0xf0f0);
    }
    qtest_quit(s);
}

static void test_events_din_rising_edge(const char *machine)
{
    QTestState *s = qtest_init(machine);
    const char path[] = "/machine/soc/sgpio[0]";
    int i;

    /* clear all inputs */
    sgpio_reset(s, 0);

    /* set all 8 input devices */
    qtest_writel(s, SGPIO(0) + GP_N_IOXCFG2, NR_SGPIO_DEVICES);

    /* set event detection type to be on the rising edge*/
    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        qtest_writel(s, SGPIO(0) + GP_N_XEVCFG(i), 0x5555);
    }
    /* Set periodic reading mode, the only accepted mode */
    qtest_writel(s, SGPIO(0) + GP_N_IOXCTS, GP_N_RD_MODE_PERIODIC);
    /* enable device, set IOXIF_EN */
    qtest_writel(s, SGPIO(0) + GP_N_IOXCTS,
                 GP_N_IOXIF_EN | GP_N_RD_MODE_PERIODIC);

    qtest_irq_intercept_in(s, "/machine/soc/gic");

    /* raise all input pin values */
    qtest_qom_set_uint64(s, path, "sgpio-pins-in", 0xffffffffffffffff);
    g_assert(qtest_qom_get_uint64(s, path, "sgpio-pins-in")
                                  == 0xffffffffffffffff);

    /* set event status to implicitly change pins */
    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        g_assert_cmphex(qtest_readb(s, SGPIO(0) + GP_N_XDIN(i)), ==, 0xff);
        g_assert_cmphex(qtest_readb(s, SGPIO(0) + GP_N_XEVSTS(i)), ==, 0xff);
        g_assert_true(qtest_get_irq(s, SGPIO_IRQ(0)));
    }

    qtest_quit(s);
}

static void test_events_din_falling_edge(const char *machine)
{
    QTestState *s = qtest_init(machine);
    const char path[] = "/machine/soc/sgpio[0]";
    int i;

    /* clear all inputs */
    sgpio_reset(s, 0);

    /* set all 8 input devices */
    qtest_writel(s, SGPIO(0) + GP_N_IOXCFG2, NR_SGPIO_DEVICES);

    /* set event detection type to be on the falling edge*/
    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        qtest_writel(s, SGPIO(0) + GP_N_XEVCFG(i), 0xaaaa);
    }
    /* Set periodic reading mode, the only accepted mode */
    qtest_writel(s, SGPIO(0) + GP_N_IOXCTS, GP_N_RD_MODE_PERIODIC);
    /* enable device, set IOXIF_EN */
    qtest_writel(s, SGPIO(0) + GP_N_IOXCTS,
                 GP_N_IOXIF_EN | GP_N_RD_MODE_PERIODIC);

    qtest_irq_intercept_in(s, "/machine/soc/gic");

    /* raise all input pin values */
    qtest_qom_set_uint64(s, path, "sgpio-pins-in", 0xffffffffffffffff);
    g_assert(qtest_qom_get_uint64(s, path, "sgpio-pins-in")
                                  == 0xffffffffffffffff);

    /* reset all input pin values */
    qtest_qom_set_uint64(s, path, "sgpio-pins-in", 0x0);
    g_assert(qtest_qom_get_uint64(s, path, "sgpio-pins-in") == 0x0);

    /* set event status to implicitly change pins */
    for (i = 0; i < NR_SGPIO_DEVICES; ++i) {
        g_assert_cmphex(qtest_readb(s, SGPIO(0) + GP_N_XDIN(i)), ==, 0x00);
        g_assert_cmphex(qtest_readb(s, SGPIO(0) + GP_N_XEVSTS(i)), ==, 0xff);
        g_assert_true(qtest_get_irq(s, SGPIO_IRQ(0)));
    }

    qtest_quit(s);
}


static void test_npcm8xx(void)
{
    test_read_dout_byte("-machine npcm845-evb");
    test_read_dout_word("-machine npcm845-evb");
    test_events_din_rising_edge("-machine npcm845-evb");
    test_events_din_falling_edge("-machine npcm845-evb");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("/npcm8xx/sgpio", test_npcm8xx);

    return g_test_run();
}
