/*
 * Remote I2C controller
 *
 * Copyright (c) 2021 Google LLC
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

#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"

#include <sys/socket.h>

#define TEST_ID "remote-i2c-test"
#define TEST_ADDR (0x62)
#define QEMU_CMD_CHR                                                           \
    " -chardev socket,id=i2c-chardev,host=localhost,port=%d,reconnect=10"

typedef enum {
    REMOTE_I2C_START_RECV = 0,
    REMOTE_I2C_START_SEND = 1,
    REMOTE_I2C_FINISH = 2,
    REMOTE_I2C_NACK = 3,
    REMOTE_I2C_RECV = 4,
    REMOTE_I2C_SEND = 5,
} RemoteI2CCommand;

static int setup_fd(int *sock)
{
    fd_set readfds;
    int fd;

    FD_ZERO(&readfds);
    FD_SET(*sock, &readfds);
    g_assert(select((*sock) + 1, &readfds, NULL, NULL, NULL) == 1);

    fd = accept(*sock, NULL, 0);
    g_assert(fd >= 0);

    return fd;
}

static void test_recv(QI2CDevice *i2cdev, int fd, uint8_t *msg, uint16_t len)
{
    uint16_t buf_size = len + 2;
    uint8_t *buf = g_new(uint8_t, buf_size);
    uint16_t bytes_read = 0;
    uint8_t zero = 0;
    ssize_t rv;

    /* write device responses to socket */
    rv = write(fd, &zero, 1);
    g_assert_cmpint(rv, ==, 1);
    rv = write(fd, msg, len);
    g_assert_cmpint(rv, ==, len);
    rv = write(fd, &zero, 1);
    g_assert_cmpint(rv, ==, 1);

    /* check received value */
    qi2c_recv(i2cdev, buf, len);
    for (int i = 0; i < len; ++i) {
        g_assert_cmphex(buf[i], ==, msg[i]);
    }

    /* check controller writes to chardev */
    do {
        bytes_read += read(fd, buf + bytes_read, buf_size - bytes_read);
    } while (bytes_read < buf_size);

    g_assert_cmphex(buf[0], ==, REMOTE_I2C_START_RECV);
    for (int i = 1; i < len - 1; ++i) {
        g_assert_cmphex(buf[i], ==, REMOTE_I2C_RECV);
    }
    g_assert_cmphex(buf[buf_size - 1], ==, REMOTE_I2C_FINISH);

    g_free(buf);
}

static void test_send(QI2CDevice *i2cdev, int fd, uint8_t *msg, uint16_t len)
{
    uint16_t buf_size = len * 2 + 2;
    uint8_t *buf = g_new0(uint8_t, buf_size);
    uint16_t bytes_read = 0;
    ssize_t rv;
    int j = 0;

    /* write device ACKs to socket*/
    rv = write(fd, buf, len + 2);
    g_assert_cmpint(rv, ==, len + 2);

    qi2c_send(i2cdev, msg, len);

    /* check controller writes to chardev */
    do {
        bytes_read += read(fd, buf + bytes_read, buf_size - bytes_read);
    } while (bytes_read < buf_size);

    g_assert_cmphex(buf[0], ==, REMOTE_I2C_START_SEND);
    for (int i = 1; i < buf_size - 1; i += 2) {
        g_assert_cmphex(buf[i], ==, REMOTE_I2C_SEND);
        g_assert_cmphex(buf[i + 1], ==, msg[j++]);
    }
    g_assert_cmphex(buf[buf_size - 1], ==, REMOTE_I2C_FINISH);

    g_free(buf);
}

static void test_remote_i2c_recv(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    int *sock = (int *)data;
    int fd = setup_fd(sock);

    uint8_t msg[] = {0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F};

    test_recv(i2cdev, fd, msg, 1);
    test_recv(i2cdev, fd, msg, 2);
    test_recv(i2cdev, fd, msg, 3);
    test_recv(i2cdev, fd, msg, 4);
    test_recv(i2cdev, fd, msg, 5);
    test_recv(i2cdev, fd, msg, 6);
    test_recv(i2cdev, fd, msg, 7);
    test_recv(i2cdev, fd, msg, 8);
    test_recv(i2cdev, fd, msg, 9);
}

static void test_remote_i2c_send(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    int *sock = (int *)data;
    int fd = setup_fd(sock);

    uint8_t msg[] = {0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F};

    test_send(i2cdev, fd, msg, 1);
    test_send(i2cdev, fd, msg, 2);
    test_send(i2cdev, fd, msg, 3);
    test_send(i2cdev, fd, msg, 4);
    test_send(i2cdev, fd, msg, 5);
    test_send(i2cdev, fd, msg, 6);
    test_send(i2cdev, fd, msg, 7);
    test_send(i2cdev, fd, msg, 8);
    test_send(i2cdev, fd, msg, 9);
}

static in_port_t open_socket(int *sock)
{
    struct sockaddr_in myaddr;
    socklen_t addrlen;

    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    myaddr.sin_port = 0;

    *sock = socket(AF_INET, SOCK_STREAM, 0);
    g_assert(*sock != -1);
    g_assert(bind(*sock, (struct sockaddr *)&myaddr, sizeof(myaddr)) != -1);

    addrlen = sizeof(myaddr);
    g_assert(getsockname(*sock, (struct sockaddr *)&myaddr, &addrlen) != -1);
    g_assert(listen(*sock, 1) != -1);

    return ntohs(myaddr.sin_port);
}

static void remote_i2c_test_cleanup(void *socket)
{
    int *s = socket;

    close(*s);
    qos_invalidate_command_line();
    g_free(s);
}

static void *remote_i2c_test_setup(GString *cmd_line, void *arg)
{
    int *sock = g_new(int, 1);

    g_string_append_printf(cmd_line, QEMU_CMD_CHR, open_socket(sock));
    g_test_queue_destroy(remote_i2c_test_cleanup, sock);
    return sock;
}

static void register_remote_i2c_test(void)
{
    QOSGraphEdgeOptions edge = {
        .extra_device_opts = "id=" TEST_ID ",address=0x62,chardev=i2c-chardev"};
    add_qi2c_address(&edge, &(QI2CAddress){TEST_ADDR});

    qos_node_create_driver("remote-i2c", i2c_device_create);
    qos_node_consumes("remote-i2c", "i2c-bus", &edge);

    QOSGraphTestOptions opts = {
        .before = remote_i2c_test_setup,
    };
    qemu_add_opts(&qemu_chardev_opts);
    qos_add_test("test_remote_i2c_recv", "remote-i2c", test_remote_i2c_recv,
                 &opts);
    qos_add_test("test_remote_i2c_send", "remote-i2c", test_remote_i2c_send,
                 &opts);
}
libqos_init(register_remote_i2c_test);
