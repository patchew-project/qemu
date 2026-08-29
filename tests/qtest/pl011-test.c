/*
 * QTest testcase for the Arm PL011 UART DMA request outputs.
 *
 * Copyright (c) 2026 Gilles Grimaud
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqtest.h"

#define PL011_BASE          0x09000000
#define PL011_DR            0x000
#define PL011_CR            0x030
#define PL011_DMACR         0x048

#define CR_RXE              (1 << 9)
#define CR_TXE              (1 << 8)
#define CR_LBE              (1 << 7)
#define CR_UARTEN           (1 << 0)

#define DMACR_RXDMAE        (1 << 0)
#define DMACR_TXDMAE        (1 << 1)

static char *find_pl011_path(QTestState *qts)
{
    g_autoptr(QDict) response = NULL;
    QListEntry *entry;
    QList *children;

    response = qtest_qmp(qts,
                         "{ 'execute': 'qom-list',"
                         "  'arguments': { 'path': '/machine/unattached' } }");
    children = qdict_get_qlist(response, "return");

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), "child<pl011>")) {
            return g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }

    g_assert_not_reached();
}

static void test_tx_dreq(void)
{
    QTestState *qts = qtest_init("-M virt");
    g_autofree char *path = find_pl011_path(qts);

    qtest_irq_intercept_out_named(qts, path, "dreq-tx");
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, PL011_BASE + PL011_DMACR, DMACR_TXDMAE);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, PL011_BASE + PL011_DMACR), ==,
                    DMACR_TXDMAE);

    qtest_writel(qts, PL011_BASE + PL011_DMACR, 0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

static void test_rx_dreq(void)
{
    QTestState *qts = qtest_init("-M virt");
    g_autofree char *path = find_pl011_path(qts);

    qtest_irq_intercept_out_named(qts, path, "dreq-rx");
    qtest_writel(qts, PL011_BASE + PL011_CR,
                 CR_RXE | CR_TXE | CR_LBE | CR_UARTEN);
    qtest_writel(qts, PL011_BASE + PL011_DMACR, DMACR_RXDMAE);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, PL011_BASE + PL011_DR, 'x');
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, PL011_BASE + PL011_DR), ==, 'x');
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/pl011/dreq/tx", test_tx_dreq);
    qtest_add_func("/pl011/dreq/rx", test_rx_dreq);

    return g_test_run();
}
