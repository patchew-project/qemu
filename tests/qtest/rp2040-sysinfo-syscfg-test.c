/*
 * QTest testcase for the RP2040 sysinfo and syscfg blocks.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define SYSINFO_BASE                  0x40000000
#define SYSINFO_CHIP_ID               0x00
#define SYSINFO_PLATFORM              0x04
#define SYSINFO_GITREF_RP2040         0x40

#define SYSINFO_PLATFORM_ASIC         BIT(1)

#define SYSCFG_BASE                   0x40004000
#define SYSCFG_PROC0_NMI_MASK         0x00
#define SYSCFG_PROC1_NMI_MASK         0x04
#define SYSCFG_PROC_CONFIG            0x08
#define SYSCFG_PROC_IN_SYNC_BYPASS    0x0c
#define SYSCFG_PROC_IN_SYNC_BYPASS_HI 0x10
#define SYSCFG_DBGFORCE               0x14
#define SYSCFG_MEMPOWERDOWN           0x18

#define SRAM0_BASE                    0x20000000
#define UART0_BASE                    0x40034000
#define UARTDR                        0x00
#define UARTCR                        0x30
#define UARTIMSC                      0x38
#define UARTICR                       0x44
#define UART_INT_TX                   BIT(5)
#define UART0_IRQ                     20
#define NVIC_ISPR                     0xe000e200
#define NVIC_ICPR                     0xe000e280
#define NVIC_ICSR                     0xe000ed04
#define NVIC_ICSR_NMIPENDSET          BIT(31)

#define PROC_CONFIG_RESET             0x10000000
#define DBGFORCE_RESET                0x00000066

#define ATOMIC_SET_ALIAS              0x2000
#define ATOMIC_CLR_ALIAS              0x3000

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_sysinfo_read_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, SYSINFO_BASE + SYSINFO_CHIP_ID), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SYSINFO_BASE + SYSINFO_PLATFORM), ==,
                    SYSINFO_PLATFORM_ASIC);
    g_assert_cmphex(qtest_readl(qts, SYSINFO_BASE + SYSINFO_GITREF_RP2040),
                    ==, 0);

    qtest_quit(qts);
}

static void test_syscfg_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_PROC1_NMI_MASK),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_PROC_CONFIG), ==,
                    PROC_CONFIG_RESET);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_DBGFORCE), ==,
                    DBGFORCE_RESET);

    qtest_quit(qts);
}

static void test_syscfg_rw_masks(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK, 0xa5a5a5a5);
    qtest_writel(qts, SYSCFG_BASE + SYSCFG_PROC1_NMI_MASK, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK),
                    ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_PROC1_NMI_MASK),
                    ==, 0x5a5a5a5a);

    qtest_writel(qts, SYSCFG_BASE + SYSCFG_PROC_IN_SYNC_BYPASS, 0xffffffff);
    qtest_writel(qts, SYSCFG_BASE + SYSCFG_PROC_IN_SYNC_BYPASS_HI,
                 0xffffffff);
    qtest_writel(qts, SYSCFG_BASE + SYSCFG_MEMPOWERDOWN, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts,
                                SYSCFG_BASE + SYSCFG_PROC_IN_SYNC_BYPASS),
                    ==, 0x3fffffff);
    g_assert_cmphex(qtest_readl(qts,
                                SYSCFG_BASE + SYSCFG_PROC_IN_SYNC_BYPASS_HI),
                    ==, 0x0000003f);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_MEMPOWERDOWN), ==,
                    0x000000ff);

    qtest_quit(qts);
}

static void test_syscfg_atomic_aliases(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK, 0);
    qtest_writel(qts, SYSCFG_BASE + ATOMIC_SET_ALIAS + SYSCFG_PROC0_NMI_MASK,
                 0x0000000f);
    qtest_writel(qts, SYSCFG_BASE + ATOMIC_CLR_ALIAS + SYSCFG_PROC0_NMI_MASK,
                 0x00000003);
    g_assert_cmphex(qtest_readl(qts, SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK),
                    ==, 0x0000000c);

    qtest_quit(qts);
}

static void test_syscfg_mempowerdown_blocks_sram(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, SRAM0_BASE, 0x11223344);
    g_assert_cmphex(qtest_readl(qts, SRAM0_BASE), ==, 0x11223344);

    qtest_writel(qts, SYSCFG_BASE + SYSCFG_MEMPOWERDOWN, BIT(0));
    qtest_writel(qts, SRAM0_BASE, 0xaabbccdd);
    qtest_writel(qts, SYSCFG_BASE + SYSCFG_MEMPOWERDOWN, 0);

    g_assert_cmphex(qtest_readl(qts, SRAM0_BASE), ==, 0x11223344);

    qtest_quit(qts);
}

static void test_syscfg_nmi_mask_routes_uart0_irq(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, UART0_BASE + UARTCR, BIT(0) | BIT(8));
    qtest_writel(qts, UART0_BASE + UARTIMSC, UART_INT_TX);

    qtest_writel(qts, UART0_BASE + UARTICR, UART_INT_TX);
    qtest_writel(qts, NVIC_ICPR, BIT(UART0_IRQ));
    qtest_writel(qts, UART0_BASE + UARTDR, 'a');
    g_assert_cmphex(qtest_readl(qts, NVIC_ISPR) & BIT(UART0_IRQ), ==,
                    BIT(UART0_IRQ));

    qtest_writel(qts, UART0_BASE + UARTICR, UART_INT_TX);
    qtest_writel(qts, NVIC_ICPR, BIT(UART0_IRQ));
    qtest_writel(qts, SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK, BIT(UART0_IRQ));
    qtest_writel(qts, UART0_BASE + UARTDR, 'b');
    g_assert_cmphex(qtest_readl(qts, NVIC_ISPR) & BIT(UART0_IRQ), ==, 0);
    g_assert_cmphex(qtest_readl(qts, NVIC_ICSR) & NVIC_ICSR_NMIPENDSET, ==,
                    NVIC_ICSR_NMIPENDSET);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-sysinfo/read-values", test_sysinfo_read_values);
    qtest_add_func("/rp2040-syscfg/reset-values", test_syscfg_reset_values);
    qtest_add_func("/rp2040-syscfg/rw-masks", test_syscfg_rw_masks);
    qtest_add_func("/rp2040-syscfg/atomic-aliases",
                   test_syscfg_atomic_aliases);
    qtest_add_func("/rp2040-syscfg/mempowerdown-blocks-sram",
                   test_syscfg_mempowerdown_blocks_sram);
    qtest_add_func("/rp2040-syscfg/nmi-mask-routes-uart0-irq",
                   test_syscfg_nmi_mask_routes_uart0_irq);

    return g_test_run();
}
