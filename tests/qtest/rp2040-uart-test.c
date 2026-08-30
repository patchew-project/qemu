/*
 * QTest testcase for the RP2040 UART pinmux integration.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define IOBANK0_BASE  0x40014000
#define GPIO_CTRL(n)  (0x004 + (n) * 8)
#define GPIO_FUNC_UART 2

#define UART0_BASE 0x40034000
#define UART1_BASE 0x40038000
#define UART_DR     0x000
#define UART_FR     0x018
#define UART_CR     0x030
#define UART_FR_RXFE 0x10

#define ATOMIC_XOR 0x1000
#define ATOMIC_SET 0x2000
#define ATOMIC_CLR 0x3000

static char *new_output_path(void)
{
    int fd;
    char *path = NULL;

    fd = g_file_open_tmp("rp2040-uart-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    return path;
}

static void assert_file_contents(const char *path, const char *expected)
{
    g_autofree char *contents = NULL;
    gsize length;

    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, strlen(expected));
    g_assert_cmpmem(contents, length, expected, strlen(expected));
}

static void test_uart0_pinmux(void)
{
    g_autofree char *path = new_output_path();
    QTestState *qts = qtest_initf("-machine raspi-pico "
                                  "-chardev file,id=uart0,path=%s "
                                  "-serial chardev:uart0", path);

    qtest_writel(qts, UART0_BASE + UART_DR, 'X');
    qtest_writel(qts, IOBANK0_BASE + GPIO_CTRL(0), GPIO_FUNC_UART);
    qtest_writel(qts, UART0_BASE + UART_DR, '0');
    qtest_quit(qts);

    assert_file_contents(path, "0");
    unlink(path);
}

static void test_uart1_pinmux(void)
{
    g_autofree char *path = new_output_path();
    QTestState *qts = qtest_initf("-machine raspi-pico -serial null "
                                  "-chardev file,id=uart1,path=%s "
                                  "-serial chardev:uart1", path);

    qtest_writel(qts, UART1_BASE + UART_DR, 'X');
    qtest_writel(qts, IOBANK0_BASE + GPIO_CTRL(4), GPIO_FUNC_UART);
    qtest_writel(qts, UART1_BASE + UART_DR, '1');
    qtest_quit(qts);

    assert_file_contents(path, "1");
    unlink(path);
}

static void test_non_strict_uart_pins(void)
{
    g_autofree char *path = new_output_path();
    QTestState *qts = qtest_initf("-machine raspi-pico,strict-uart-pins=off "
                                  "-chardev file,id=uart0,path=%s "
                                  "-serial chardev:uart0", path);

    qtest_writel(qts, UART0_BASE + UART_DR, 'L');
    qtest_quit(qts);

    assert_file_contents(path, "L");
    unlink(path);
}

static void test_uart0_rx_pinmux(void)
{
    int sock_fd;
    int retries;
    QTestState *qts = qtest_init_with_serial("-machine raspi-pico",
                                             &sock_fd);

    qtest_writel(qts, IOBANK0_BASE + GPIO_CTRL(1), GPIO_FUNC_UART);
    g_assert_cmpint(send(sock_fd, "R", 1, 0), ==, 1);
    for (retries = 0; retries < 1000; retries++) {
        if (!(qtest_readl(qts, UART0_BASE + UART_FR) & UART_FR_RXFE)) {
            break;
        }
        g_usleep(1000);
    }
    g_assert_cmpint(retries, <, 1000);
    g_assert_cmphex(qtest_readl(qts, UART0_BASE + UART_DR), ==, 'R');

    close(sock_fd);
    qtest_quit(qts);
}

static void test_atomic_aliases(void)
{
    QTestState *qts = qtest_init("-machine raspi-pico");
    uint32_t reset = qtest_readl(qts, UART0_BASE + UART_CR);

    qtest_writel(qts, UART0_BASE + ATOMIC_CLR + UART_CR, 0x100);
    g_assert_cmphex(qtest_readl(qts, UART0_BASE + UART_CR), ==,
                    reset & ~0x100);
    qtest_writel(qts, UART0_BASE + ATOMIC_SET + UART_CR, 0x100);
    g_assert_cmphex(qtest_readl(qts, UART0_BASE + UART_CR), ==, reset);
    qtest_writel(qts, UART0_BASE + ATOMIC_XOR + UART_CR, 0x200);
    g_assert_cmphex(qtest_readl(qts, UART0_BASE + UART_CR), ==,
                    reset ^ 0x200);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-uart/uart0-pinmux", test_uart0_pinmux);
    qtest_add_func("/rp2040-uart/uart1-pinmux", test_uart1_pinmux);
    qtest_add_func("/rp2040-uart/non-strict", test_non_strict_uart_pins);
    qtest_add_func("/rp2040-uart/uart0-rx-pinmux", test_uart0_rx_pinmux);
    qtest_add_func("/rp2040-uart/atomic-aliases", test_atomic_aliases);

    return g_test_run();
}
