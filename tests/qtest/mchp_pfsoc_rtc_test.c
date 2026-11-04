/*
 * QTest testcase for the Microchip PolarFire SoC RTC
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "libqtest.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"

#define RTC_BASE            0x20124000
#define RTC_CONTROL         0x00
#define RTC_MODE            0x04
#define RTC_PRESCALER       0x08
#define RTC_ALARM_LOWER     0x0c
#define RTC_ALARM_UPPER     0x10
#define RTC_COMPARE_LOWER   0x14
#define RTC_COMPARE_UPPER   0x18
#define RTC_DATETIME_LOWER  0x20
#define RTC_DATETIME_UPPER  0x24
#define RTC_RUNNING         BIT(0)
#define RTC_STOP            BIT(1)
#define RTC_ALARM_ON        BIT(2)
#define RTC_ALARM_OFF       BIT(3)
#define RTC_UPLOAD          BIT(5)
#define RTC_MATCH           BIT(7)
#define RTC_WAKEUP_CLEAR    BIT(8)
#define RTC_WAKEUP_SET      BIT(9)
#define RTC_UPDATED         BIT(10)
#define RTC_WAKEUP_ENABLE   BIT(1)
#define RTC_WAKEUP_RESET    BIT(2)
#define RTC_WAKEUP_CONTINUE BIT(3)
#define RTC_PRESCALER_MASK  0x03ffffff
#define RTC_UPPER_MASK      0x3fffffff
#define PLIC_BASE           0x0c000000
#define PLIC_PENDING        0x1000
#define PLIC_RTC_PENDING    (BIT(16) | BIT(17))
#define ENVM_BASE           0x20220000
#define HSS_HEADER_SIZE     0x100
#define HSS_HEADER_JUMP     0x1000006f
#define HSS_PAYLOAD_BASE    (ENVM_BASE + HSS_HEADER_SIZE)
#define HSS_PAYLOAD_SIZE    8

static QTestState *microchip_icicle_kit_start_rtc(const char *firmware)
{
    return qtest_initf(
        "-machine microchip-icicle-kit -smp 5 -m 2G -bios \"%s\" "
        "-rtc clock=vm", firmware);
}

static QTestState *microchip_icicle_kit_start_ext(const char *qemu_var,
                                                  const char *firmware)
{
    g_autofree char *args = g_strdup_printf(
        "-machine microchip-icicle-kit -smp 5 -m 2G -bios \"%s\"",
        firmware);

    return qtest_init_ext(qemu_var, args, NULL, true);
}

static char *create_firmware(void)
{
    uint8_t firmware[HSS_HEADER_SIZE + HSS_PAYLOAD_SIZE] = { 0 };
    static const uint8_t payload[] = {
        0x13, 0x00, 0x00, 0x00,
        0x6f, 0x00, 0x00, 0x00,
    };
    char *path;
    int fd;
    ssize_t written;
    unsigned int i;

    stl_le_p(&firmware[0], HSS_HEADER_JUMP);
    stl_le_p(&firmware[4], HSS_PAYLOAD_SIZE);
    for (i = 0; i < 5; i++) {
        stl_le_p(&firmware[8 + i * sizeof(uint32_t)], HSS_PAYLOAD_BASE);
    }
    memcpy(&firmware[HSS_HEADER_SIZE], payload, sizeof(payload));

    fd = g_file_open_tmp("microchip-icicle-kit-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);

    written = write(fd, firmware, sizeof(firmware));
    g_assert_cmpint(written, ==, sizeof(firmware));
    close(fd);

    return path;
}

static uint64_t rtc_read_binary_count(QTestState *qts)
{
    uint64_t count = qtest_readl(qts, RTC_BASE + RTC_DATETIME_LOWER);

    count |= (uint64_t)qtest_readl(qts,
                                   RTC_BASE + RTC_DATETIME_UPPER) << 32;
    return count;
}

static void test_rtc(void)
{
    g_autofree char *firmware = create_firmware();
    QTestState *qts = microchip_icicle_kit_start_rtc(firmware);
    uint32_t control;

    unlink(firmware);
    qtest_irq_intercept_out_named(qts, "/machine/soc/rtc", "sysbus-irq");

    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL), ==, 0);
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 0);
    qtest_clock_step(qts, 2 * NANOSECONDS_PER_SECOND);
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 0);

    qtest_writel(qts, RTC_BASE + RTC_MODE, UINT32_MAX);
    qtest_writel(qts, RTC_BASE + RTC_PRESCALER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_MODE), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_PRESCALER), ==,
                    RTC_PRESCALER_MASK);
    qtest_writel(qts, RTC_BASE + RTC_MODE, 0);

    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_STOP);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL) &
                    RTC_RUNNING, ==, 0);
    qtest_clock_step(qts, 2 * NANOSECONDS_PER_SECOND);
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 0);

    qtest_writel(qts, RTC_BASE + RTC_DATETIME_LOWER, 42);
    qtest_writel(qts, RTC_BASE + RTC_DATETIME_UPPER, 0);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_UPLOAD);
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 42);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL) &
                    (RTC_UPLOAD | RTC_UPDATED), ==, RTC_UPDATED);

    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_RUNNING);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 43);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_WAKEUP_SET);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_WAKEUP_CLEAR);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, RTC_BASE + RTC_ALARM_LOWER, 45);
    qtest_writel(qts, RTC_BASE + RTC_ALARM_UPPER, 0);
    qtest_writel(qts, RTC_BASE + RTC_COMPARE_LOWER, UINT32_MAX);
    qtest_writel(qts, RTC_BASE + RTC_COMPARE_UPPER, RTC_UPPER_MASK);
    qtest_writel(qts, RTC_BASE + RTC_MODE,
                 RTC_WAKEUP_ENABLE | RTC_WAKEUP_CONTINUE);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_ALARM_ON);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_get_irq(qts, 1));
    control = qtest_readl(qts, RTC_BASE + RTC_CONTROL);
    g_assert_cmphex(control & RTC_ALARM_ON, ==, RTC_ALARM_ON);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, control | RTC_ALARM_OFF);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL) &
                    RTC_ALARM_ON, ==, 0);
    qtest_clock_step(qts, 2 * NANOSECONDS_PER_SECOND);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_get_irq(qts, 1));

    qtest_writel(qts, RTC_BASE + RTC_DATETIME_LOWER, 43);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_UPLOAD);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_ALARM_ON);
    qtest_clock_step(qts, 2 * NANOSECONDS_PER_SECOND);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_true(qtest_get_irq(qts, 1));
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL) & RTC_MATCH,
                    ==, RTC_MATCH);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_ALARM_OFF);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_get_irq(qts, 1));

    qtest_writel(qts, RTC_BASE + RTC_DATETIME_LOWER, 0);
    qtest_writel(qts, RTC_BASE + RTC_DATETIME_UPPER, 0);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_UPLOAD);
    qtest_writel(qts, RTC_BASE + RTC_ALARM_LOWER, 2);
    qtest_writel(qts, RTC_BASE + RTC_MODE,
                 RTC_WAKEUP_ENABLE | RTC_WAKEUP_RESET |
                 RTC_WAKEUP_CONTINUE);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_ALARM_ON);
    qtest_clock_step(qts, 2 * NANOSECONDS_PER_SECOND);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL) &
                    RTC_ALARM_ON, ==, RTC_ALARM_ON);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_WAKEUP_CLEAR);
    qtest_clock_step(qts, 2 * NANOSECONDS_PER_SECOND);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(rtc_read_binary_count(qts), ==, 0);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_ALARM_OFF);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_get_irq(qts, 1));

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_CONTROL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_MODE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RTC_BASE + RTC_PRESCALER), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_get_irq(qts, 1));

    qtest_quit(qts);
}

static void test_rtc_plic(void)
{
    g_autofree char *firmware = create_firmware();
    QTestState *qts = microchip_icicle_kit_start_rtc(firmware);

    unlink(firmware);

    qtest_writel(qts, RTC_BASE + RTC_DATETIME_LOWER, 42);
    qtest_writel(qts, RTC_BASE + RTC_DATETIME_UPPER, 0);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_UPLOAD);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_RUNNING);
    qtest_writel(qts, RTC_BASE + RTC_ALARM_LOWER, 43);
    qtest_writel(qts, RTC_BASE + RTC_ALARM_UPPER, 0);
    qtest_writel(qts, RTC_BASE + RTC_COMPARE_LOWER, UINT32_MAX);
    qtest_writel(qts, RTC_BASE + RTC_COMPARE_UPPER, RTC_UPPER_MASK);
    qtest_writel(qts, RTC_BASE + RTC_MODE,
                 RTC_WAKEUP_ENABLE | RTC_WAKEUP_CONTINUE);
    qtest_writel(qts, RTC_BASE + RTC_CONTROL, RTC_ALARM_ON);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);

    g_assert_cmphex(qtest_readl(qts, PLIC_BASE + PLIC_PENDING + 8) &
                    PLIC_RTC_PENDING, ==, PLIC_RTC_PENDING);

    qtest_quit(qts);
}

static void test_machine_migration(void)
{
    g_autofree char *firmware = create_firmware();
    const char *old_qemu = g_getenv("QTEST_QEMU_BINARY_OLD");
    QTestState *from;
    QTestState *to = qtest_initf(
        "-machine microchip-icicle-kit -smp 5 -m 2G "
        "-bios \"%s\" -incoming defer "
        "-rtc clock=vm", firmware);

    qtest_irq_intercept_out_named(to, "/machine/soc/rtc", "sysbus-irq");

    /* Allow an older QEMU source to exercise migration compatibility */
    if (old_qemu) {
        from = microchip_icicle_kit_start_ext("QTEST_QEMU_BINARY_OLD",
                                              firmware);
    } else {
        from = microchip_icicle_kit_start_rtc(firmware);
        qtest_writel(from, RTC_BASE + RTC_DATETIME_LOWER, 0x23456789);
        qtest_writel(from, RTC_BASE + RTC_DATETIME_UPPER, 1);
        qtest_writel(from, RTC_BASE + RTC_CONTROL, RTC_UPLOAD);
        qtest_writel(from, RTC_BASE + RTC_CONTROL, RTC_RUNNING);
        qtest_writel(from, RTC_BASE + RTC_ALARM_LOWER, 0x2345678b);
        qtest_writel(from, RTC_BASE + RTC_ALARM_UPPER, 1);
        qtest_writel(from, RTC_BASE + RTC_COMPARE_LOWER, UINT32_MAX);
        qtest_writel(from, RTC_BASE + RTC_COMPARE_UPPER, RTC_UPPER_MASK);
        qtest_writel(from, RTC_BASE + RTC_MODE,
                     RTC_WAKEUP_ENABLE | RTC_WAKEUP_CONTINUE);
        qtest_writel(from, RTC_BASE + RTC_CONTROL, RTC_ALARM_ON);
        qtest_writel(from, RTC_BASE + RTC_CONTROL, RTC_WAKEUP_SET);
        qtest_writel(from, RTC_BASE + RTC_PRESCALER, 999999);
    }

    unlink(firmware);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);

    if (old_qemu) {
        g_assert_cmphex(qtest_readl(to, RTC_BASE + RTC_CONTROL), ==, 0);
        g_assert_cmphex(qtest_readl(to, RTC_BASE + RTC_PRESCALER), ==, 0);
        g_assert_false(qtest_get_irq(to, 0));
        g_assert_false(qtest_get_irq(to, 1));
    } else {
        g_assert_cmphex(rtc_read_binary_count(to), ==, 0x123456789);
        g_assert_cmphex(qtest_readl(to, RTC_BASE + RTC_CONTROL) &
                        (RTC_RUNNING | RTC_ALARM_ON), ==,
                        RTC_RUNNING | RTC_ALARM_ON);
        g_assert_cmphex(qtest_readl(to, RTC_BASE + RTC_PRESCALER), ==,
                        999999);
        g_assert_true(qtest_get_irq(to, 0));
        g_assert_false(qtest_get_irq(to, 1));
        qtest_writel(to, RTC_BASE + RTC_CONTROL, RTC_WAKEUP_CLEAR);
        g_assert_false(qtest_get_irq(to, 0));
        qtest_clock_step(to, 2 * NANOSECONDS_PER_SECOND);
        g_assert_cmphex(rtc_read_binary_count(to), ==, 0x12345678b);
        g_assert_true(qtest_get_irq(to, 0));
        g_assert_true(qtest_get_irq(to, 1));
        qtest_writel(to, RTC_BASE + RTC_CONTROL, RTC_ALARM_OFF);
        g_assert_false(qtest_get_irq(to, 0));
        g_assert_false(qtest_get_irq(to, 1));
    }

    qtest_quit(from);
    qtest_quit(to);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/mchp_pfsoc_rtc/registers", test_rtc);
    qtest_add_func("/mchp_pfsoc_rtc/plic", test_rtc_plic);
    qtest_add_func("/mchp_pfsoc_rtc/migration",
                   test_machine_migration);

    return g_test_run();
}
