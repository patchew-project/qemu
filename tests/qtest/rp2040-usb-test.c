/*
 * QTest testcase for the shallow RP2040 USB controller model.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define USBCTRL_DPRAM_BASE       0x50100000
#define USBCTRL_REGS_BASE        0x50110000
#define USBCTRL_ALIAS_XOR        0x1000
#define USBCTRL_ALIAS_SET        0x2000
#define USBCTRL_ALIAS_CLR        0x3000

#define USBCTRL_SIE_STATUS       0x50
#define USBCTRL_USB_MUXING       0x74
#define USBCTRL_INTR             0x8c
#define USBCTRL_INTE             0x90
#define USBCTRL_INTF             0x94
#define USBCTRL_INTS             0x98

#define USBCTRL_VBUS_DETECTED    (1U << 11)

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_usb_dpram(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, USBCTRL_DPRAM_BASE + 0x20, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, USBCTRL_DPRAM_BASE + 0x20),
                    ==, 0x12345678);

    qtest_quit(qts);
}

static void test_usb_registers(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, USBCTRL_REGS_BASE +
                               USBCTRL_SIE_STATUS) &
                    USBCTRL_VBUS_DETECTED, ==, USBCTRL_VBUS_DETECTED);

    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_USB_MUXING, 0x5);
    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_ALIAS_SET +
                 USBCTRL_USB_MUXING, 0x8);
    g_assert_cmphex(qtest_readl(qts, USBCTRL_REGS_BASE +
                               USBCTRL_USB_MUXING), ==, 0xd);
    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_ALIAS_XOR +
                 USBCTRL_USB_MUXING, 0x9);
    g_assert_cmphex(qtest_readl(qts, USBCTRL_REGS_BASE +
                               USBCTRL_USB_MUXING), ==, 0x4);
    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_ALIAS_CLR +
                 USBCTRL_USB_MUXING, 0x4);
    g_assert_cmphex(qtest_readl(qts, USBCTRL_REGS_BASE +
                               USBCTRL_USB_MUXING), ==, 0);

    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_INTR, 0x3);
    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_INTF, 0x4);
    qtest_writel(qts, USBCTRL_REGS_BASE + USBCTRL_INTE, 0x6);
    g_assert_cmphex(qtest_readl(qts, USBCTRL_REGS_BASE + USBCTRL_INTS),
                    ==, 0x6);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-usb/dpram", test_usb_dpram);
    qtest_add_func("/rp2040-usb/registers", test_usb_registers);

    return g_test_run();
}
