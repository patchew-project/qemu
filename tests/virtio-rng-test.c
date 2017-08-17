/*
 * QTest testcase for VirtIO RNG
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/virtio.h"

#define PCI_SLOT_HP             0x06

/* Tests only initialization so far. TODO: Replace with functional tests */
static void rng_nop(void)
{
}

static void hotplug(void)
{
    qvirtio_plug_device_test("virtio-rng", "rng1", PCI_SLOT_HP, NULL);

    qvirtio_unplug_device_test("rng1", PCI_SLOT_HP);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/rng/nop", rng_nop);
    qtest_add_func("/virtio/rng/hotplug", hotplug);

    qtest_start("-device virtio-rng");
    ret = g_test_run();

    qtest_end();

    return ret;
}
