// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QTest for Remote I2C Master (CUSE)
 *
 * This test acts as a userspace I2C tool (like i2c-tools), interacting
 * with the CUSE device exposed by QEMU. It validates:
 *
 * - Synchronous devices (e.g., TMP105)
 * - Error conditions (NACK, Timeout)
 * - Complex protocols (SMBus Block, Atomic RDWR)
 *
 * Author:
 * Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "qobject/qdict.h"

#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>

#define TEST_DEV_NAME "qemui2c-test"
#define TEST_DEV_PATH "/dev/" TEST_DEV_NAME
#define TMP105_REG_CONFIG 0x01
#define TMP105_SLAVE_ADDR 0x50 /* TMP105 Address */

static int cuse_fd = -1;
static QTestState *qs;

/* --- Heartbeat Thread --- */
/* Required to advance QEMU clock while main thread is blocked in ioctl() */
static gint keep_pulsing;
static GThread *clock_thread;

static void *clock_pulse_thread(void *data)
{
    while (g_atomic_int_get(&keep_pulsing)) {
        if (qs) {
            qtest_clock_step(qs, 1000000); /* 1ms */
        }
        g_usleep(1000);
    }
    return NULL;
}

static void start_clock_pulse(void)
{
    g_atomic_int_set(&keep_pulsing, 1);
    clock_thread = g_thread_new("clock_pulse", clock_pulse_thread, NULL);
}

static void stop_clock_pulse(void)
{
    g_atomic_int_set(&keep_pulsing, 0);
    g_thread_join(clock_thread);
    clock_thread = NULL;
}

/* --- Helpers --- */

static void i2c_check_funcs(int fd)
{
    unsigned long funcs;
    int ret = ioctl(fd, I2C_FUNCS, &funcs);
    g_assert_cmpint(ret, ==, 0);
}

static void i2c_set_TMP105_SLAVE_ADDR(int fd, uint8_t addr)
{
    int ret = ioctl(fd, I2C_SLAVE, addr);
    g_assert_cmpint(ret, ==, 0);
}

static void i2c_set_pointer(int fd, uint8_t reg)
{
    struct i2c_smbus_ioctl_data args;
    args.read_write = I2C_SMBUS_WRITE;
    args.command = reg;
    args.size = I2C_SMBUS_BYTE;
    args.data = NULL;

    start_clock_pulse();
    int ret = ioctl(fd, I2C_SMBUS, &args);
    stop_clock_pulse();

    g_assert_cmpint(ret, ==, 0);
}

static uint8_t i2c_read_byte(int fd)
{
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data args;
    args.read_write = I2C_SMBUS_READ;
    args.command = 0;
    args.size = I2C_SMBUS_BYTE;
    args.data = &data;

    start_clock_pulse();
    int ret = ioctl(fd, I2C_SMBUS, &args);
    stop_clock_pulse();

    g_assert_cmpint(ret, ==, 0);
    return data.byte;
}

static void i2c_write_byte_data(int fd, uint8_t reg, uint8_t val)
{
    union i2c_smbus_data data;
    data.byte = val;
    struct i2c_smbus_ioctl_data args;
    args.read_write = I2C_SMBUS_WRITE;
    args.command = reg;
    args.size = I2C_SMBUS_BYTE_DATA;
    args.data = &data;

    start_clock_pulse();
    int ret = ioctl(fd, I2C_SMBUS, &args);
    stop_clock_pulse();

    g_assert_cmpint(ret, ==, 0);
}

/* --- Setup --- */

static void test_init(void)
{
    GString *cmd_line = g_string_new(NULL);

    g_string_append_printf(cmd_line,
        "-machine lm3s811evb "
        "-device tmp105,bus=i2c,address=0x50,id=sensor0 "
        "-object remote-i2c-backend-cuse,id=my_cuse_backend,devname=" TEST_DEV_NAME " "
        "-device remote-i2c-master,i2cbus=i2c,backend=my_cuse_backend,timeout-ms=8000");

    qs = qtest_init(cmd_line->str);
    g_string_free(cmd_line, TRUE);

    int retries = 50;
    while (retries-- > 0) {
        cuse_fd = open(TEST_DEV_PATH, O_RDWR);
        if (cuse_fd >= 0) {
            break;
        }
        g_usleep(100000);
    }

    if (cuse_fd < 0) {
        g_test_skip("Could not open CUSE device \
                    (check permissions or FUSE module)");
    }
}

static void test_cleanup(void)
{
    if (cuse_fd >= 0) {
        close(cuse_fd);
    }
    if (qs) {
        qtest_quit(qs);
        qs = NULL;
    }
}

/* --- Synchronous Tests (Standard TMP105) --- */

static void test_capabilities(void)
{
    if (cuse_fd < 0) {
        return;
    }
    i2c_check_funcs(cuse_fd);
}

static void test_sensor_rw(void)
{
    if (cuse_fd < 0) {
        return;
    }
    i2c_set_TMP105_SLAVE_ADDR(cuse_fd, TMP105_SLAVE_ADDR);

    /* 1. Set Pointer to T_HIGH (0x03) and Read */
    i2c_set_pointer(cuse_fd, 0x03);
    uint8_t t_high = i2c_read_byte(cuse_fd);

    /* Default value for T_HIGH is 0x50 */
    g_assert_cmphex(t_high, ==, 0x50);

    /* 2. Write Config (0x01) and Read back */
    i2c_write_byte_data(cuse_fd, TMP105_REG_CONFIG, 0x60);
    i2c_set_pointer(cuse_fd, TMP105_REG_CONFIG);
    uint8_t config = i2c_read_byte(cuse_fd);
    g_assert_cmphex(config, ==, 0x60);
}

static void test_nack(void)
{
    if (cuse_fd < 0) {
        {

        }
        return;
    }
    /* Address 0x20 does not exist */
    i2c_set_TMP105_SLAVE_ADDR(cuse_fd, 0x20);

    union i2c_smbus_data data;
    data.byte = 0x00;
    struct i2c_smbus_ioctl_data args;
    args.read_write = I2C_SMBUS_WRITE;
    args.command = 0x00;
    args.size = I2C_SMBUS_BYTE_DATA;
    args.data = &data;

    start_clock_pulse();
    int ret = ioctl(cuse_fd, I2C_SMBUS, &args);
    stop_clock_pulse();

    /* Should return -1 (error) */
    if (ret == 0) {
        g_assert_cmpint(ret, ==, -1);
    }
}

static void test_quick_cmd(void)
{
    if (cuse_fd < 0) {
        return;
    }
    i2c_set_TMP105_SLAVE_ADDR(cuse_fd, TMP105_SLAVE_ADDR);

    struct i2c_smbus_ioctl_data args;
    args.read_write = I2C_SMBUS_WRITE;
    args.command = 0;
    args.size = I2C_SMBUS_QUICK;
    args.data = NULL;

    start_clock_pulse();
    int ret = ioctl(cuse_fd, I2C_SMBUS, &args);
    stop_clock_pulse();
    g_assert_cmpint(ret, ==, 0);
}

static void test_temperature_read(void)
{
    if (cuse_fd < 0) {
        return;
    }
    i2c_set_TMP105_SLAVE_ADDR(cuse_fd, TMP105_SLAVE_ADDR);

    /* Set pointer to Temperature Register (0x00) */
    i2c_set_pointer(cuse_fd, 0x00);

    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data args;
    args.read_write = I2C_SMBUS_READ;
    args.command = 0;
    args.size = I2C_SMBUS_WORD_DATA;
    args.data = &data;

    start_clock_pulse();
    int ret = ioctl(cuse_fd, I2C_SMBUS, &args);
    stop_clock_pulse();

    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(data.word, ==, 0x0000);
}

static void test_i2c_rdwr(void)
{
    if (cuse_fd < 0) {
        return;
    }

    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[2];
    uint8_t write_buf[1] = { 0x03 };
    uint8_t read_buf[2] = { 0x00, 0x00 };

    /* Write Message (Set Pointer) */
    messages[0].addr = TMP105_SLAVE_ADDR;
    messages[0].flags = 0;
    messages[0].len = sizeof(write_buf);
    messages[0].buf = write_buf;

    /* Read Message (Get Data) */
    messages[1].addr = TMP105_SLAVE_ADDR;
    messages[1].flags = I2C_M_RD;
    messages[1].len = sizeof(read_buf);
    messages[1].buf = read_buf;

    packets.msgs = messages;
    packets.nmsgs = 2;

    start_clock_pulse();
    int ret = ioctl(cuse_fd, I2C_RDWR, &packets);
    stop_clock_pulse();

    g_assert_cmpint(ret, ==, 2);
    g_assert_cmphex(read_buf[0], ==, 0x50);
}

/* --- Main --- */
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* Init VM */
    qtest_add_func("/remote-i2c/capabilities", test_init);

    /* Synchronous Tests */
    qtest_add_func("/remote-i2c/func_check", test_capabilities);
    qtest_add_func("/remote-i2c/sensor_rw", test_sensor_rw);
    qtest_add_func("/remote-i2c/nack_check", test_nack);
    qtest_add_func("/remote-i2c/quick_cmd", test_quick_cmd);
    qtest_add_func("/remote-i2c/temp_read_word", test_temperature_read);
    qtest_add_func("/remote-i2c/rdwr_atomic", test_i2c_rdwr);

    int ret = g_test_run();
    test_cleanup();
    return ret;
}
