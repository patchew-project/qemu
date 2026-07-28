/*
 * QTest for the xiic-fpga-i2c PCIe FPGA and its embedded xlnx-axi-iic cores.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"

#define XIIC_IISR   0x20
#define XIIC_IIER   0x28
#define XIIC_DGIER  0x1C
#define XIIC_SR     0x104
#define XIIC_DTR    0x108
#define XIIC_DRR    0x10C

#define XIIC_SR_RX_FIFO_EMPTY   0x40
#define XIIC_SR_TX_FIFO_EMPTY   0x80
#define XIIC_SR_BUS_BUSY        0x04

#define XIIC_INTR_TX_ERROR      0x02
#define XIIC_INTR_BNB           0x10

#define XIIC_DYN_START          0x100
#define XIIC_DYN_STOP           0x200

#define TMP105_ADDR             0x4c
#define TMP105_REG_CONFIG       0x01

typedef struct {
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;
} XiicFixture;

static void save_dev(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **out = data;

    if (*out) {
        g_free(dev);
    } else {
        *out = dev;
    }
}

static void fixture_setup(XiicFixture *f)
{
    f->qts = qtest_init("-device xiic-fpga-i2c,num-channels=1 "
                        "-device tmp105,id=temp,bus=xiic-fpga-i2c.0,address=0x4c");
    f->pcibus = qpci_new_pc(f->qts, NULL);
    f->dev = NULL;
    qpci_device_foreach(f->pcibus, 0x10ee, 0x7021, save_dev, &f->dev);
    g_assert(f->dev != NULL);
    qpci_device_enable(f->dev);
    f->bar = qpci_iomap(f->dev, 0, NULL);
}

static void fixture_teardown(XiicFixture *f)
{
    qpci_iounmap(f->dev, f->bar);
    g_free(f->dev);
    qpci_free_pc(f->pcibus);
    qtest_quit(f->qts);
}

static void wr(XiicFixture *f, uint64_t off, uint32_t val)
{
    qpci_io_writel(f->dev, f->bar, off, val);
}

static uint32_t rd(XiicFixture *f, uint64_t off)
{
    return qpci_io_readl(f->dev, f->bar, off);
}

static void test_registers(void)
{
    XiicFixture f;
    uint32_t sr;

    fixture_setup(&f);

    sr = rd(&f, XIIC_SR);
    g_assert_cmphex(sr & XIIC_SR_TX_FIFO_EMPTY, ==, XIIC_SR_TX_FIFO_EMPTY);
    g_assert_cmphex(sr & XIIC_SR_RX_FIFO_EMPTY, ==, XIIC_SR_RX_FIFO_EMPTY);
    g_assert_cmphex(sr & XIIC_SR_BUS_BUSY, ==, 0);

    wr(&f, XIIC_IIER, 0x08);
    g_assert_cmphex(rd(&f, XIIC_IIER), ==, 0x08);
    wr(&f, XIIC_DGIER, 0x80000000);
    g_assert_cmphex(rd(&f, XIIC_DGIER), ==, 0x80000000);

    fixture_teardown(&f);
}

static void test_read(void)
{
    XiicFixture f;

    fixture_setup(&f);

    qtest_qmp_assert_success(f.qts,
        "{ 'execute': 'qom-set', 'arguments':"
        " { 'path': '/machine/peripheral/temp',"
        "   'property': 'temperature', 'value': 21000 } }");

    wr(&f, XIIC_DTR, XIIC_DYN_START | (TMP105_ADDR << 1) | 1);
    wr(&f, XIIC_DTR, XIIC_DYN_STOP | 2);

    g_assert_cmphex(rd(&f, XIIC_DRR) & 0xff, ==, 0x15);

    fixture_teardown(&f);
}

static void test_write(void)
{
    XiicFixture f;
    uint32_t isr;

    fixture_setup(&f);

    wr(&f, XIIC_DTR, XIIC_DYN_START | (TMP105_ADDR << 1) | 0);
    wr(&f, XIIC_DTR, TMP105_REG_CONFIG);
    wr(&f, XIIC_DTR, XIIC_DYN_STOP | 0x00);

    isr = rd(&f, XIIC_IISR);
    g_assert_cmphex(isr & XIIC_INTR_TX_ERROR, ==, 0);
    g_assert_cmphex(isr & XIIC_INTR_BNB, ==, XIIC_INTR_BNB);
    g_assert_cmphex(rd(&f, XIIC_SR) & XIIC_SR_BUS_BUSY, ==, 0);

    fixture_teardown(&f);
}

static void test_nack(void)
{
    XiicFixture f;

    fixture_setup(&f);

    wr(&f, XIIC_DTR, XIIC_DYN_START | (0x20 << 1) | 1);
    g_assert_cmphex(rd(&f, XIIC_IISR) & XIIC_INTR_TX_ERROR, ==,
                    XIIC_INTR_TX_ERROR);

    fixture_teardown(&f);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/xiic-fpga-i2c/registers", test_registers);
    qtest_add_func("/xiic-fpga-i2c/read", test_read);
    qtest_add_func("/xiic-fpga-i2c/write", test_write);
    qtest_add_func("/xiic-fpga-i2c/nack", test_nack);
    return g_test_run();
}
