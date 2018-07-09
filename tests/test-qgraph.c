/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/qgraph.h"
#include "libqos/qgraph_extra.h"

#define MACHINE_PC "x86_64/pc"
#define MACHINE_RASPI2 "arm/raspi2"
#define I440FX "i440FX-pcihost"
#define PCIBUS_PC "pcibus-pc"
#define SDHCI "sdhci"
#define PCIBUS "pci-bus"
#define SDHCI_PCI "sdhci-pci"
#define SDHCI_MM "generic-sdhci"
#define REGISTER_TEST "register-test"

int npath;

static void *machinefunct(void)
{
    return NULL;
}

static void *driverfunct(void *obj, QOSGraphObject *machine)
{
    return NULL;
}

static void testfunct(void *obj, void *arg)
{
    return;
}

static void check_machine(const char *machine)
{
    qos_node_create_machine(machine, machinefunct);
    g_assert_nonnull(qos_graph_get_machine(machine));
    g_assert_cmpint(qos_graph_has_machine(machine), ==, TRUE);
    g_assert_nonnull(qos_graph_get_node(machine));
    g_assert_cmpint(qos_graph_get_node_availability(machine), ==, FALSE);
    qos_graph_node_set_availability(machine, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(machine), ==, TRUE);
    g_assert_cmpint(qos_graph_has_node(machine), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(machine), ==, MACHINE);
}

static void check_contains(const char *machine, const char *i440fx)
{
    qos_node_contains(machine, i440fx);
    g_assert_nonnull(qos_graph_get_edge(machine, i440fx));
    g_assert_cmpint(qos_graph_get_edge_type(machine, i440fx), ==, CONTAINS);
    g_assert_cmpint(qos_graph_has_edge(machine, i440fx), ==, TRUE);
}

static void check_produces(const char *machine, const char *i440fx)
{
    qos_node_produces(machine, i440fx);
    g_assert_nonnull(qos_graph_get_edge(machine, i440fx));
    g_assert_cmpint(qos_graph_get_edge_type(machine, i440fx), ==, PRODUCES);
    g_assert_cmpint(qos_graph_has_edge(machine, i440fx), ==, TRUE);
}

static void check_consumes(const char *interface, const char *driver)
{
    qos_node_consumes(driver, interface);
    g_assert_nonnull(qos_graph_get_edge(interface, driver));
    g_assert_cmpint(
        qos_graph_get_edge_type(interface, driver), ==, CONSUMED_BY);
    g_assert_cmpint(qos_graph_has_edge(interface, driver), ==, TRUE);
}

static void check_driver(const char *driver)
{
    qos_node_create_driver(driver, driverfunct);
    g_assert_cmpint(qos_graph_has_machine(driver), ==, FALSE);
    g_assert_nonnull(qos_graph_get_node(driver));
    g_assert_cmpint(qos_graph_has_node(driver), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(driver), ==, DRIVER);
    g_assert_cmpint(qos_graph_get_node_availability(driver), ==, FALSE);
    qos_graph_node_set_availability(driver, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(driver), ==, TRUE);
}

static void check_interface(const char *interface)
{
    qos_node_create_interface(interface);
    g_assert_cmpint(qos_graph_has_machine(interface), ==, FALSE);
    g_assert_nonnull(qos_graph_get_node(interface));
    g_assert_cmpint(qos_graph_has_node(interface), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(interface), ==, INTERFACE);
    g_assert_cmpint(qos_graph_get_node_availability(interface), ==, FALSE);
    qos_graph_node_set_availability(interface, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(interface), ==, TRUE);
}

static void check_test(const char *test, const char *interface)
{
    qos_add_test(test, interface, testfunct);
    g_assert_cmpint(qos_graph_has_machine(test), ==, FALSE);
    g_assert_nonnull(qos_graph_get_node(test));
    g_assert_cmpint(qos_graph_has_node(test), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(test), ==, TEST);
    g_assert_nonnull(qos_graph_get_edge(interface, test));
    g_assert_cmpint(qos_graph_get_edge_type(interface, test), ==, CONSUMED_BY);
    g_assert_cmpint(qos_graph_has_edge(interface, test), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(test), ==, TRUE);
    qos_graph_node_set_availability(test, FALSE);
    g_assert_cmpint(qos_graph_get_node_availability(test), ==, FALSE);
}

static void count_each_test(QOSGraphNode *path, int len)
{
    npath++;
}

static void check_leaf_discovered(int n)
{
    npath = 0;
    qos_graph_foreach_test_path(count_each_test);
    g_assert_cmpint(n, ==, npath);
}

/* G_Test functions */

static void init_nop(void)
{
    qos_graph_init();
    qos_graph_destroy();
}

static void test_machine(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    qos_graph_destroy();
}

static void test_contains(void)
{
    qos_graph_init();
    check_contains(MACHINE_PC, I440FX);
    g_assert_null(qos_graph_get_machine(MACHINE_PC));
    g_assert_null(qos_graph_get_machine(I440FX));
    g_assert_null(qos_graph_get_node(MACHINE_PC));
    g_assert_null(qos_graph_get_node(I440FX));
    qos_graph_destroy();
}

static void test_multiple_contains(void)
{
    qos_graph_init();
    check_contains(MACHINE_PC, I440FX);
    check_contains(MACHINE_PC, PCIBUS_PC);
    qos_graph_destroy();
}

static void test_produces(void)
{
    qos_graph_init();
    check_produces(MACHINE_PC, I440FX);
    g_assert_null(qos_graph_get_machine(MACHINE_PC));
    g_assert_null(qos_graph_get_machine(I440FX));
    g_assert_null(qos_graph_get_node(MACHINE_PC));
    g_assert_null(qos_graph_get_node(I440FX));
    qos_graph_destroy();
}

static void test_multiple_produces(void)
{
    qos_graph_init();
    check_produces(MACHINE_PC, I440FX);
    check_produces(MACHINE_PC, PCIBUS_PC);
    qos_graph_destroy();
}

static void test_consumed_by(void)
{
    qos_graph_init();
    check_consumes(SDHCI, I440FX);
    g_assert_null(qos_graph_get_machine(I440FX));
    g_assert_null(qos_graph_get_machine(SDHCI));
    g_assert_null(qos_graph_get_node(I440FX));
    g_assert_null(qos_graph_get_node(SDHCI));
    qos_graph_destroy();
}

static void test_multiple_consumed_by(void)
{
    qos_graph_init();
    check_consumes(SDHCI, I440FX);
    check_consumes(SDHCI, PCIBUS_PC);
    qos_graph_destroy();
}

static void test_driver(void)
{
    qos_graph_init();
    check_driver(I440FX);
    qos_graph_destroy();
}

static void test_interface(void)
{
    qos_graph_init();
    check_interface(SDHCI);
    qos_graph_destroy();
}

static void test_test(void)
{
    qos_graph_init();
    check_test(REGISTER_TEST, SDHCI);
    qos_graph_destroy();
}

static void test_machine_contains_driver(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_driver(I440FX);
    check_contains(MACHINE_PC, I440FX);
    qos_graph_destroy();
}

static void test_driver_contains_driver(void)
{
    qos_graph_init();
    check_driver(PCIBUS_PC);
    check_driver(I440FX);
    check_contains(PCIBUS_PC, I440FX);
    qos_graph_destroy();
}

static void test_machine_produces_interface(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_interface(SDHCI);
    check_produces(MACHINE_PC, SDHCI);
    qos_graph_destroy();
}

static void test_driver_produces_interface(void)
{
    qos_graph_init();
    check_driver(I440FX);
    check_interface(SDHCI);
    check_produces(I440FX, SDHCI);
    qos_graph_destroy();
}

static void test_interface_consumed_by_machine(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_interface(SDHCI);
    check_consumes(SDHCI, MACHINE_PC);
    qos_graph_destroy();
}

static void test_interface_consumed_by_driver(void)
{
    qos_graph_init();
    check_driver(I440FX);
    check_interface(SDHCI);
    check_consumes(SDHCI, I440FX);
    qos_graph_destroy();
}

static void test_interface_consumed_by_test(void)
{
    qos_graph_init();
    check_interface(SDHCI);
    check_test(REGISTER_TEST, SDHCI);
    qos_graph_destroy();
}

static void test_full_sample(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_contains(MACHINE_PC, I440FX);
    check_driver(I440FX);
    check_driver(PCIBUS_PC);
    check_contains(I440FX, PCIBUS_PC);
    check_interface(PCIBUS);
    check_produces(PCIBUS_PC, PCIBUS);
    check_driver(SDHCI_PCI);
    qos_node_consumes(SDHCI_PCI, PCIBUS);
    check_produces(SDHCI_PCI, SDHCI);
    check_interface(SDHCI);
    check_driver(SDHCI_MM);
    check_produces(SDHCI_MM, SDHCI);
    qos_add_test(REGISTER_TEST, SDHCI, testfunct);
    check_leaf_discovered(1);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_full_sample_raspi(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_contains(MACHINE_PC, I440FX);
    check_driver(I440FX);
    check_driver(PCIBUS_PC);
    check_contains(I440FX, PCIBUS_PC);
    check_interface(PCIBUS);
    check_produces(PCIBUS_PC, PCIBUS);
    check_driver(SDHCI_PCI);
    qos_node_consumes(SDHCI_PCI, PCIBUS);
    check_produces(SDHCI_PCI, SDHCI);
    check_interface(SDHCI);
    check_machine(MACHINE_RASPI2);
    check_contains(MACHINE_RASPI2, SDHCI_MM);
    check_driver(SDHCI_MM);
    check_produces(SDHCI_MM, SDHCI);
    qos_add_test(REGISTER_TEST, SDHCI, testfunct);
    qos_print_graph();
    check_leaf_discovered(2);
    qos_graph_destroy();
}

static void test_full_alternative_path(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_driver("B");
    check_driver("C");
    check_driver("D");
    check_driver("E");
    check_driver("F");
    check_contains(MACHINE_RASPI2, "B");
    check_contains("B", "C");
    check_contains("C", "D");
    check_contains("D", "E");
    check_contains("D", "F");
    qos_add_test("G", "D", testfunct);
    check_contains("F", "G");
    check_contains("E", "B");
    qos_print_graph();
    check_leaf_discovered(2);
    qos_graph_destroy();
}

static void test_cycle(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_driver("B");
    check_driver("C");
    check_driver("D");
    check_contains(MACHINE_RASPI2, "B");
    check_contains("B", "C");
    check_contains("C", "D");
    check_contains("D", MACHINE_RASPI2);
    check_leaf_discovered(0);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_two_test_same_interface(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_interface("B");
    qos_add_test("C", "B", testfunct);
    qos_add_test("D", "B", testfunct);
    check_contains(MACHINE_RASPI2, "B");
    check_leaf_discovered(2);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_double_edge(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_driver("B");
    check_driver("C");
    check_produces("B", "C");
    qos_node_consumes("C", "B");
    qos_add_test("D", "C", testfunct);
    check_contains(MACHINE_RASPI2, "B");
    qos_print_graph();
    qos_graph_destroy();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qgraph/init_nop", init_nop);
    g_test_add_func("/qgraph/test_machine", test_machine);
    g_test_add_func("/qgraph/test_contains", test_contains);
    g_test_add_func("/qgraph/test_multiple_contains", test_multiple_contains);
    g_test_add_func("/qgraph/test_produces", test_produces);
    g_test_add_func("/qgraph/test_multiple_produces", test_multiple_produces);
    g_test_add_func("/qgraph/test_consumed_by", test_consumed_by);
    g_test_add_func("/qgraph/test_multiple_consumed_by",
                        test_multiple_consumed_by);
    g_test_add_func("/qgraph/test_driver", test_driver);
    g_test_add_func("/qgraph/test_interface", test_interface);
    g_test_add_func("/qgraph/test_test", test_test);
    g_test_add_func("/qgraph/test_machine_contains_driver",
                        test_machine_contains_driver);
    g_test_add_func("/qgraph/test_driver_contains_driver",
                        test_driver_contains_driver);
    g_test_add_func("/qgraph/test_machine_produces_interface",
                        test_machine_produces_interface);
    g_test_add_func("/qgraph/test_driver_produces_interface",
                        test_driver_produces_interface);
    g_test_add_func("/qgraph/test_interface_consumed_by_machine",
                        test_interface_consumed_by_machine);
    g_test_add_func("/qgraph/test_interface_consumed_by_driver",
                        test_interface_consumed_by_driver);
    g_test_add_func("/qgraph/test_interface_consumed_by_test",
                        test_interface_consumed_by_test);
    g_test_add_func("/qgraph/test_full_sample", test_full_sample);
    g_test_add_func("/qgraph/test_full_sample_raspi", test_full_sample_raspi);
    g_test_add_func("/qgraph/test_full_alternative_path",
                        test_full_alternative_path);
    g_test_add_func("/qgraph/test_cycle", test_cycle);
    g_test_add_func("/qgraph/test_two_test_same_interface",
                        test_two_test_same_interface);
    g_test_add_func("/qgraph/test_double_edge", test_double_edge);

    g_test_run();
    return 0;
}
