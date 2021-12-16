/*
 * QTest testcase for the Google GPIO Transmitter, using the NPCM7xx GPIO
 * controller.
 *
 * Copyright 2021 Google LLC
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
#include "libqos/libqtest.h"

#define NR_GPIO_DEVICES (8)
#define GPIO(x)         (0xf0010000 + (x) * 0x1000)

/* GPIO registers */
#define GP_N_TLOCK1     0x00
#define GP_N_DIN        0x04 /* Data IN */
#define GP_N_POL        0x08 /* Polarity */
#define GP_N_DOUT       0x0c /* Data OUT */
#define GP_N_OE         0x10 /* Output Enable */
#define GP_N_OTYP       0x14
#define GP_N_MP         0x18
#define GP_N_PU         0x1c /* Pull-up */
#define GP_N_PD         0x20 /* Pull-down */
#define GP_N_DBNC       0x24 /* Debounce */
#define GP_N_EVTYP      0x28 /* Event Type */
#define GP_N_EVBE       0x2c /* Event Both Edge */
#define GP_N_OBL0       0x30
#define GP_N_OBL1       0x34
#define GP_N_OBL2       0x38
#define GP_N_OBL3       0x3c
#define GP_N_EVEN       0x40 /* Event Enable */
#define GP_N_EVENS      0x44 /* Event Set (enable) */
#define GP_N_EVENC      0x48 /* Event Clear (disable) */
#define GP_N_EVST       0x4c /* Event Status */
#define GP_N_SPLCK      0x50
#define GP_N_MPLCK      0x54
#define GP_N_IEM        0x58 /* Input Enable */
#define GP_N_OSRC       0x5c
#define GP_N_ODSC       0x60
#define GP_N_DOS        0x68 /* Data OUT Set */
#define GP_N_DOC        0x6c /* Data OUT Clear */
#define GP_N_OES        0x70 /* Output Enable Set */
#define GP_N_OEC        0x74 /* Output Enable Clear */
#define GP_N_TLOCK2     0x7c

#define PACKET_REVISION 0x01

typedef enum {
    GPIOTXCODE_OK              = 0x00,
    GPIOTXCODE_MALFORMED_PKT   = 0xe0,
    GPIOTXCODE_UNKNOWN_VERSION = 0xe1,
} GPIOTXCode;

static int sock;
static int fd;

static in_port_t open_socket(void)
{
    struct sockaddr_in myaddr;
    struct timeval timeout = { .tv_sec = 1, };
    socklen_t addrlen;

    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    myaddr.sin_port = 0;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    g_assert(sock != -1);
    g_assert(bind(sock, (struct sockaddr *) &myaddr, sizeof(myaddr)) != -1);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    addrlen = sizeof(myaddr);
    g_assert(getsockname(sock, (struct sockaddr *) &myaddr , &addrlen) != -1);
    g_assert(listen(sock, 1) != -1);
    return ntohs(myaddr.sin_port);
}

static void setup_fd(void)
{
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    g_assert(select(sock + 1, &readfds, NULL, NULL, NULL) == 1);

    fd = accept(sock, NULL, 0);
}

/*
 * The GPIO controller is naturally chatty and will send us state updates when
 * any register is written to, since it could impact the GPIO state.
 * For our purposes, we only care when we set DOUT, so we use this to discard

 * state changes we don't care about.
 */
static void purge_read_queue(void)
{
    uint8_t buf[256];
    ssize_t ret;

    do {
        ret = read(fd, buf, sizeof(buf));
    } while (ret > 0);
}

static void gpio_unlock(int n)
{
    if (readl(GPIO(n) + GP_N_TLOCK1) != 0) {
        writel(GPIO(n) + GP_N_TLOCK2, 0xc0de1248);
        writel(GPIO(n) + GP_N_TLOCK1, 0xc0defa73);
    }
}

/* Restore the GPIO controller to a sensible default state. */
static void gpio_reset(int n)
{
    gpio_unlock(0);

    writel(GPIO(n) + GP_N_EVEN, 0x00000000);
    writel(GPIO(n) + GP_N_EVST, 0xffffffff);
    writel(GPIO(n) + GP_N_POL, 0x00000000);
    writel(GPIO(n) + GP_N_DOUT, 0x00000000);
    writel(GPIO(n) + GP_N_OE, 0x00000000);
    writel(GPIO(n) + GP_N_OTYP, 0x00000000);
    writel(GPIO(n) + GP_N_PU, 0xffffffff);
    writel(GPIO(n) + GP_N_PD, 0x00000000);
    writel(GPIO(n) + GP_N_IEM, 0xffffffff);
}

static void set_dout(int n, uint32_t val)
{
    gpio_reset(n);
    writel(GPIO(n) + GP_N_OE, 0xffffffff);
    /* Remove anything the controller TXed from reset and OEN */
    purge_read_queue();

    writel(GPIO(n) + GP_N_DOUT, val);
    g_assert_cmphex(readl(GPIO(n) + GP_N_DOUT), ==, val);
}

static void read_data(uint8_t *data, size_t len)
{
    ssize_t ret;
    size_t len_read = 0;

    while (len_read < len) {
        ret = read(fd, &data[len_read], len);
        g_assert_cmpint(ret, !=, -1);

        len_read += ret;
    }
}

/*
 * Set DOUT, ensure only the allowed pin triggers a packet tx, then receive the
 * state update TXed by the controller.
 */
static void test_gpio_n_tx(gconstpointer test_data)
{
    uint8_t packet[6];
    uint32_t gpio_state;
    intptr_t n = (intptr_t)test_data;
    uint8_t resp;
    ssize_t ret;

    set_dout(n, 0xaa55aa55);
    read_data(packet, sizeof(packet));
    gpio_state = *(uint32_t *)&packet[2];

    g_assert_cmpint(packet[0], ==, PACKET_REVISION);
    g_assert_cmpint(packet[1], ==, n);
    g_assert_cmpint(gpio_state, ==, 0xaa55aa55);

    /* All good */
    resp = 0x00;
    ret = write(fd, &resp, sizeof(resp));
    g_assert_cmpint(ret, !=, -1);
}

int main(int argc, char **argv)
{
    int ret;
    size_t i;
    int port;

    g_test_init(&argc, &argv, NULL);
    port = open_socket();

    global_qtest = qtest_initf("-machine npcm750-evb "
                "-chardev socket,id=google-gpio-tx-chr,port=%d,host=localhost "
                "-global driver=google.gpio-transmitter,property=gpio-chardev,"
                "value=google-gpio-tx-chr",
                port);
    setup_fd();

    for (i = 0; i < NR_GPIO_DEVICES; i++) {
        g_autofree char *test_name =
            g_strdup_printf("/google_gpio_tx/gpio[%zu]/tx", i);
        qtest_add_data_func(test_name, (void *)(intptr_t)i, test_gpio_n_tx);
    }

    ret = g_test_run();
    qtest_end();

    return ret;
}
