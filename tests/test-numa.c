/*
 * QEMU NUMA testing
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"

#include "sysemu/numa_int.h"

static void test_numa_parse(const char **nodestr)
{
    QemuOpts *opts;
    size_t i;

    DECLARE_BITMAP(node0cpus, MAX_CPUMASK_BITS);
    DECLARE_BITMAP(node5cpus, MAX_CPUMASK_BITS);

    bitmap_zero(node0cpus, MAX_CPUMASK_BITS);
    bitmap_zero(node5cpus, MAX_CPUMASK_BITS);
    for (i = 0; i <= 3; i++) {
        bitmap_set(node0cpus, i, 1);
    }
    for (i = 8; i <= 11; i++) {
        bitmap_set(node0cpus, i, 1);
    }
    for (i = 4; i <= 7; i++) {
        bitmap_set(node5cpus, i, 1);
    }
    for (i = 12; i <= 15; i++) {
        bitmap_set(node5cpus, i, 1);
    }

    max_cpus = 16;

    opts = qemu_opts_parse_noisily(&qemu_numa_opts,
                                   nodestr[0], true);
    g_assert(opts != NULL);

    opts = qemu_opts_parse_noisily(&qemu_numa_opts,
                                   nodestr[1], true);
    g_assert(opts != NULL);

    qemu_opts_foreach(&qemu_numa_opts, parse_numa, NULL, NULL);

    g_assert_cmpint(max_numa_nodeid, ==, 6);
    g_assert(!have_memdevs);

    g_assert_cmpint(nb_numa_nodes, ==, 2);
    for (i = 0; i < MAX_NODES; i++) {
        if (i == 0 || i == 5) {
            g_assert(numa_info[i].present);
            g_assert_cmpint(numa_info[i].node_mem, ==, 107 * 1024 * 1024);

            if (i == 0) {
                g_assert(bitmap_equal(node0cpus,
                                      numa_info[i].node_cpu,
                                      MAX_CPUMASK_BITS));
            } else {
                g_assert(bitmap_equal(node5cpus,
                                      numa_info[i].node_cpu,
                                      MAX_CPUMASK_BITS));
            }
        } else {
            g_assert(!numa_info[i].present);
        }
    }

    nb_numa_nodes = 0;
    max_numa_nodeid = 0;
    memset(&numa_info, 0, sizeof(numa_info));
    g_assert(!numa_info[0].present);
    qemu_opts_reset(&qemu_numa_opts);
}

static void test_numa_parse_legacy(void)
{
    const char *nodestr[] = {
        "node,nodeid=0,cpus=0-3,cpus=8-11,mem=107",
        "node,nodeid=5,cpus=4-7,cpus=12-15,mem=107"
    };
    test_numa_parse(nodestr);
}

static void test_numa_parse_modern(void)
{
    const char *nodestr[] = {
        "type=node,data.nodeid=0,data.cpus.0=0,data.cpus.1=1,data.cpus.2=2,data.cpus.3=3,"
          "data.cpus.4=8,data.cpus.5=9,data.cpus.6=10,data.cpus.7=11,data.mem=107",
        "type=node,data.nodeid=5,data.cpus.0=4,data.cpus.1=5,data.cpus.2=6,data.cpus.3=7,"
          "data.cpus.4=12,data.cpus.5=13,data.cpus.6=14,data.cpus.7=15,data.mem=107",
    };
    test_numa_parse(nodestr);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/numa/parse/legacy", test_numa_parse_legacy);
    g_test_add_func("/numa/parse/modern", test_numa_parse_modern);
    return g_test_run();
}
