/*
 * Kvaser PCI SJA1000 receive FIFO tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"

typedef struct {
    QPCIDevice *dev;
    QPCIBar bar;
} Controller;

static void write_reg(Controller *c, unsigned reg, uint8_t value)
{
    qpci_io_writeb(c->dev, c->bar, reg, value);
}

static uint8_t read_reg(Controller *c, unsigned reg)
{
    return qpci_io_readb(c->dev, c->bar, reg);
}

static void map_controller(Controller *c, QPCIBus *bus, int slot)
{
    c->dev = qpci_device_find(bus, QPCI_DEVFN(slot, 0));
    g_assert_nonnull(c->dev);
    qpci_device_enable(c->dev);
    c->bar = qpci_iomap(c->dev, 1, NULL);
}

static void setup_controller(Controller *c, QPCIBus *bus, int slot)
{
    map_controller(c, bus, slot);
    write_reg(c, 31, 0x80);
    write_reg(c, 0, 0x09);
    for (int i = 20; i < 24; ++i) {
        write_reg(c, i, 0xff);
    }
    write_reg(c, 0, 0x08);
}

static void setup_basic_controller(Controller *c, QPCIBus *bus, int slot)
{
    map_controller(c, bus, slot);
    write_reg(c, 31, 0);
    write_reg(c, 0, 0x01);
    write_reg(c, 4, 0);
    write_reg(c, 5, 0xff);
    write_reg(c, 0, 0);
}

static void send_frame(Controller *c, bool extended, bool rtr,
                       unsigned dlc, uint32_t id)
{
    unsigned data_reg;

    write_reg(c, 16, (extended ? 0x80 : 0) | (rtr ? 0x40 : 0) | dlc);
    if (extended) {
        write_reg(c, 17, id >> 21);
        write_reg(c, 18, id >> 13);
        write_reg(c, 19, id >> 5);
        write_reg(c, 20, id << 3);
        data_reg = 21;
    } else {
        write_reg(c, 17, id >> 3);
        write_reg(c, 18, id << 5);
        data_reg = 19;
    }
    for (unsigned i = 0; i < dlc; ++i) {
        write_reg(c, data_reg + i, 0xa0 + i);
    }
    write_reg(c, 1, 0x01);
}

static void expect_frame(Controller *c, bool extended, bool rtr,
                         unsigned dlc, uint32_t id)
{
    uint32_t actual_id;
    unsigned data_reg;

    g_assert_cmphex(read_reg(c, 2) & 1, ==, 1);
    g_assert_cmphex(read_reg(c, 16), ==,
                    (extended ? 0x80 : 0) | (rtr ? 0x40 : 0) | dlc);
    if (extended) {
        actual_id = (uint32_t)read_reg(c, 17) << 21 |
                    (uint32_t)read_reg(c, 18) << 13 |
                    (uint32_t)read_reg(c, 19) << 5 |
                    read_reg(c, 20) >> 3;
        data_reg = 21;
    } else {
        actual_id = (uint32_t)read_reg(c, 17) << 3 | read_reg(c, 18) >> 5;
        data_reg = 19;
    }
    g_assert_cmphex(actual_id, ==, id);
    if (!rtr) {
        for (unsigned i = 0; i < dlc; ++i) {
            g_assert_cmphex(read_reg(c, data_reg + i), ==, 0xa0 + i);
        }
    }
    write_reg(c, 1, 0x04);
}

static void send_basic_frame(Controller *c, bool rtr, unsigned dlc,
                             uint32_t id)
{
    write_reg(c, 10, id >> 3);
    write_reg(c, 11, (id << 5) | (rtr ? 0x10 : 0) | dlc);
    for (unsigned i = 0; i < dlc; ++i) {
        write_reg(c, 12 + i, 0xa0 + i);
    }
    write_reg(c, 1, 0x01);
}

static void expect_basic_frame(Controller *c, bool rtr, unsigned dlc,
                               uint32_t id)
{
    g_assert_cmphex(read_reg(c, 2) & 1, ==, 1);
    g_assert_cmphex(read_reg(c, 21) & 0x1f, ==, (rtr ? 0x10 : 0) | dlc);
    g_assert_cmphex((uint32_t)read_reg(c, 20) << 3 |
                    read_reg(c, 21) >> 5, ==, id);
    if (!rtr) {
        for (unsigned i = 0; i < dlc; ++i) {
            g_assert_cmphex(read_reg(c, 22 + i), ==, 0xa0 + i);
        }
    }
    write_reg(c, 1, 0x04);
}

static void test_rtr_fifo(void)
{
    QTestState *qts = qtest_init("-machine pc -nodefaults "
                                 "-object can-bus,id=can0 "
                                 "-device kvaser_pci,canbus=can0,addr=04.0 "
                                 "-device kvaser_pci,canbus=can0,addr=05.0");
    QPCIBus *bus = qpci_new_pc(qts, NULL);
    Controller tx, rx;

    setup_controller(&tx, bus, 4);
    setup_controller(&rx, bus, 5);
    for (int extended = 0; extended <= 1; ++extended) {
        for (unsigned dlc = 0; dlc <= 8; ++dlc) {
            uint32_t id = extended ? 0x1abcdeff : 0x5e1;
            uint32_t data_id = extended ? 0x1abcdef0 : 0x5e2;

            send_frame(&tx, extended, true, dlc, id);
            send_frame(&tx, extended, false, 8, data_id);
            expect_frame(&rx, extended, true, dlc, id);
            expect_frame(&rx, extended, false, 8, data_id);
            g_assert_cmphex(read_reg(&rx, 2) & 1, ==, 0);
        }
    }
    g_free(tx.dev);
    g_free(rx.dev);
    qpci_free_pc(bus);
    qtest_quit(qts);
}

static void test_reset_fifo(void)
{
    QTestState *qts = qtest_init("-machine pc -nodefaults "
                                 "-object can-bus,id=can0 "
                                 "-device kvaser_pci,canbus=can0,addr=04.0 "
                                 "-device kvaser_pci,canbus=can0,addr=05.0");
    QPCIBus *bus = qpci_new_pc(qts, NULL);
    Controller tx, rx;

    setup_controller(&tx, bus, 4);
    setup_controller(&rx, bus, 5);
    send_frame(&tx, false, false, 8, 0x5e2);
    expect_frame(&rx, false, false, 8, 0x5e2);
    qpci_iounmap(tx.dev, tx.bar);
    qpci_iounmap(rx.dev, rx.bar);
    g_free(tx.dev);
    g_free(rx.dev);
    qpci_free_pc(bus);

    qtest_system_reset(qts);
    bus = qpci_new_pc(qts, NULL);
    setup_controller(&tx, bus, 4);
    setup_controller(&rx, bus, 5);
    send_frame(&tx, false, false, 8, 0x5e3);
    expect_frame(&rx, false, false, 8, 0x5e3);
    g_free(tx.dev);
    g_free(rx.dev);
    qpci_free_pc(bus);
    qtest_quit(qts);
}

static void test_basiccan_rtr_fifo(void)
{
    QTestState *qts = qtest_init("-machine pc -nodefaults "
                                 "-object can-bus,id=can0 "
                                 "-device kvaser_pci,canbus=can0,addr=04.0 "
                                 "-device kvaser_pci,canbus=can0,addr=05.0");
    QPCIBus *bus = qpci_new_pc(qts, NULL);
    Controller tx, rx;

    setup_basic_controller(&tx, bus, 4);
    setup_basic_controller(&rx, bus, 5);
    for (unsigned dlc = 0; dlc <= 8; ++dlc) {
        send_basic_frame(&tx, true, dlc, 0x5e1);
        send_basic_frame(&tx, false, 8, 0x5e2);
        expect_basic_frame(&rx, true, dlc, 0x5e1);
        expect_basic_frame(&rx, false, 8, 0x5e2);
        g_assert_cmphex(read_reg(&rx, 2) & 1, ==, 0);
    }
    g_free(tx.dev);
    g_free(rx.dev);
    qpci_free_pc(bus);
    qtest_quit(qts);
}

static void test_software_reset_fifo(void)
{
    QTestState *qts = qtest_init("-machine pc -nodefaults "
                                 "-object can-bus,id=can0 "
                                 "-device kvaser_pci,canbus=can0,addr=04.0 "
                                 "-device kvaser_pci,canbus=can0,addr=05.0");
    QPCIBus *bus = qpci_new_pc(qts, NULL);
    Controller tx, rx;

    setup_controller(&tx, bus, 4);
    map_controller(&rx, bus, 5);
    write_reg(&rx, 5, 0xff);
    write_reg(&rx, 0, 0);

    send_frame(&tx, false, false, 1, 0x5e2);
    g_assert_cmphex(read_reg(&rx, 2) & 1, ==, 1);
    g_assert_cmphex(read_reg(&rx, 20), ==, 0x5e2 >> 3);
    write_reg(&rx, 1, 0x04);

    write_reg(&rx, 0, 0);
    send_frame(&tx, false, false, 1, 0x123);
    g_assert_cmphex(read_reg(&rx, 2) & 1, ==, 1);
    g_assert_cmphex(read_reg(&rx, 20), ==, 0x123 >> 3);
    g_assert_cmphex(read_reg(&rx, 21), ==, ((0x123 << 5) & 0xe0) | 1);
    g_assert_cmphex(read_reg(&rx, 22), ==, 0xa0);
    write_reg(&rx, 1, 0x04);

    g_free(tx.dev);
    g_free(rx.dev);
    qpci_free_pc(bus);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/kvaser-pci/rtr-fifo", test_rtr_fifo);
    qtest_add_func("/kvaser-pci/basiccan-rtr-fifo", test_basiccan_rtr_fifo);
    qtest_add_func("/kvaser-pci/reset-fifo", test_reset_fifo);
    qtest_add_func("/kvaser-pci/software-reset-fifo", test_software_reset_fifo);
    return g_test_run();
}
