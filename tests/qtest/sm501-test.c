/*
 * QEMU test for the SM501 companion
 *
 * SPDX-FileCopyrightText: 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu-common.h"
#include "libqtest.h"
#include "libqos/libqos-spapr.h"
#include "hw/pci/pci_ids.h"

typedef struct {
    QOSState *qs;
    QPCIDevice *dev;
    QPCIBar bar;
} PciSm501State;

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    *((QPCIDevice **)data) = dev;
}

static void sm501_init(PciSm501State *s)
{
    uint64_t barsize;

    s->dev = NULL;
    qpci_device_foreach(s->qs->pcibus, PCI_VENDOR_ID_SILICON_MOTION,
                        PCI_DEVICE_ID_SM501, save_fn, &s->dev);
    g_assert(s->dev != NULL);

    qpci_device_enable(s->dev);

    /* BAR#0: VRAM, BAR#1: MMIO registers */
    s->bar = qpci_iomap(s->dev, 1, &barsize);
    g_assert_cmpuint(barsize, ==, 2 * MiB);
}

static void sm501_deinit(PciSm501State *s)
{
    g_free(s->dev);
}

static uint32_t sm501_read(PciSm501State *s, uint64_t off)
{
    uint32_t val;

    s->dev->bus->memread(s->dev->bus, s->bar.addr + off, &val, sizeof(val));

    return val;
}

static void sm501_write(PciSm501State *s, uint64_t off, uint32_t val)
{
    s->dev->bus->memwrite(s->dev->bus, s->bar.addr + off, &val, sizeof(val));
}

static void sm501_check_device_id(PciSm501State *s)
{
    g_assert_cmphex(sm501_read(s, 0x60) >> 16, ==, 0x501); /* DEVICEID reg */
}

/*
 * Try to reproduce the heap overflow reported in
 * https://bugzilla.redhat.com/show_bug.cgi?id=1786026
 */
static void test_sm501_2d_drawing_engine_op(void)
{
    PciSm501State s;

    s.qs = qtest_spapr_boot("-machine pseries -device sm501");

    sm501_init(&s);
    sm501_check_device_id(&s);

    /*
     * Commands listed in BZ#1786026 to access
     * COPY_AREA() in sm501_2d_operation().
     */
    sm501_write(&s, 0x100000,        0x0);  /* src: (x, y) = (0, 0) */
    sm501_write(&s, 0x100004,        0x0);  /* dst: (x, y) = (0, 0) */
    sm501_write(&s, 0x100008, 0x00100010);  /* dim: height = width = 16 */
    sm501_write(&s, 0x100010, 0x00100010);  /* pitch: height = width = 16 */
    sm501_write(&s, 0x10000c, 0xcc000088);  /* ctrl: op = copy area, RTL */

    /* If the overflow occured, the next call will fail. */
    sm501_check_device_id(&s);

    sm501_deinit(&s);

    qtest_shutdown(s.qs);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    if (!strcmp(qtest_get_arch(), "ppc64")) {
        qtest_add_func("spapr/sm501_2d_op", test_sm501_2d_drawing_engine_op);
    }

    return g_test_run();
}
