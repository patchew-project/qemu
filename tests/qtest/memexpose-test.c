/*
 *  Memexpose PCI device
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "libqos/libqos-pc.h"
#include "libqtest-single.h"
#include "hw/misc/memexpose/memexpose-core.h"

static char *tmpshm;
static char *tmpdir;

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **pdev = (QPCIDevice **) data;

    *pdev = dev;
}

static QPCIDevice *get_device(QPCIBus *pcibus)
{
    QPCIDevice *dev;

    dev = NULL;
    qpci_device_foreach(pcibus, 0x1af4, 0x1111, save_fn, &dev);
    g_assert(dev != NULL);

    return dev;
}

typedef struct _MexpState {
    QOSState *qs;
    QPCIBar reg_bar, mem_bar;
    QPCIDevice *dev;
} MexpState;


static inline void read_mexp_mem(MexpState *s, uint64_t off,
                                 void *buf, size_t len)
{
    qpci_memread(s->dev, s->mem_bar, off, buf, len);
}

static inline void write_mexp_mem(MexpState *s, uint64_t off,
                                  const void *buf, size_t len)
{
    qpci_memwrite(s->dev, s->mem_bar, off, buf, len);
}

static inline void read_mem(MexpState *s, uint64_t off,
                            void *buf, size_t len)
{
    char *cbuf = buf;
    for (size_t i = 0; i < len; i++) {
        cbuf[i] = qtest_readb(s->qs->qts, off + i);
    }
}

static inline void write_mem(MexpState *s, uint64_t off,
                             const void *buf, size_t len)
{
    const char *cbuf = buf;
    for (size_t i = 0; i < len; i++) {
        qtest_writeb(s->qs->qts, off + i, cbuf[i]);
    }
}

static inline void write_mexp_reg(MexpState *s, uint64_t off,
                                  uint64_t val)
{
    qpci_io_writeq(s->dev, s->reg_bar, off, val);
}

static inline uint64_t read_mexp_reg(MexpState *s, uint64_t off)
{
    return qpci_io_readq(s->dev, s->reg_bar, off);
}

static void mexp_send_intr(MexpState *s, uint64_t type,
                           uint64_t data)
{
    uint64_t send = 1;
    write_mexp_reg(s, MEMEXPOSE_INTR_TX_TYPE_ADDR, type);
    write_mexp_reg(s, MEMEXPOSE_INTR_TX_DATA_ADDR, data);
    write_mexp_reg(s, MEMEXPOSE_INTR_SEND_ADDR, send);
}

static uint64_t mexp_recv_intr(MexpState *s, uint64_t *type,
                               uint64_t *data)
{
    uint64_t recv = 0;
    int tries = 0;
    while (recv == 0 && tries < 100) {
        recv = read_mexp_reg(s, MEMEXPOSE_INTR_RECV_ADDR);
        if (recv) {
            break;
        }
        tries++;
        g_usleep(10000);
    }
    *type = read_mexp_reg(s, MEMEXPOSE_INTR_RX_TYPE_ADDR);
    *data = read_mexp_reg(s, MEMEXPOSE_INTR_RX_DATA_ADDR);
    return recv;
}

static void setup_vm_cmd(MexpState *s, const char *cmd, bool msix)
{
    uint64_t barsize;
    const char *arch = qtest_get_arch();

    if (strcmp(arch, "x86_64") == 0) {
        s->qs = qtest_pc_boot(cmd);
    } else {
        g_printerr("memexpose-test tests are only available on x86_64\n");
        exit(EXIT_FAILURE);
    }
    s->dev = get_device(s->qs->pcibus);
    s->reg_bar = qpci_iomap(s->dev, 0, &barsize);
    g_assert_cmpuint(barsize, ==, MEMEXPOSE_INTR_MEM_SIZE);

    if (msix) {
        qpci_msix_enable(s->dev);
    }

    s->mem_bar = qpci_iomap(s->dev, 1, &barsize);

    qpci_device_enable(s->dev);
}

static void remove_socks(char *tmp_path)
{
    char *memsock = g_strdup_printf("%s/qemu-mexp-mem", tmp_path);
    g_remove(memsock);
    g_free(memsock);

    char *intsock = g_strdup_printf("%s/qemu-mexp-mem", tmp_path);
    g_remove(intsock);
    g_free(intsock);
}
static void add_socks(char *tmp_path)
{
    char *memsock = g_strdup_printf("%s/qemu-mexp-mem", tmp_path);
    mkfifo(memsock, 0700);
    g_free(memsock);

    char *intsock = g_strdup_printf("%s/qemu-mexp-mem", tmp_path);
    mkfifo(intsock, 0700);
    g_free(intsock);
}

static void setup_vm(MexpState *s, int server)
{
    unsigned long shm_size = 1 << 28;
    const char *socksrv = server ? "server,nowait," : "";
    char *cmd = g_strdup_printf("-mem-path %s "
                                "-device memexpose-pci,mem_chardev=mem-mem,"
                                "intr_chardev=mem-intr,shm_size=0x%lx "
                                "-chardev socket,%spath=%s/qemu-mexp-mem,id=mem-mem "
                                "-chardev socket,%spath=%s/qemu-mexp-intr,id=mem-intr",
                                tmpshm, shm_size,
                                socksrv, tmpdir, socksrv, tmpdir);
    setup_vm_cmd(s, cmd, false);
    g_free(cmd);
}

static void cleanup_vm(MexpState *s)
{
    assert(!global_qtest);
    g_free(s->dev);
    qtest_shutdown(s->qs);
}

static void setup_connected_vms(MexpState *s1, MexpState *s2)
{
    remove_socks(tmpdir);
    add_socks(tmpdir);
    setup_vm(s1, 1);
    setup_vm(s2, 0);

    write_mexp_reg(s1, MEMEXPOSE_INTR_ENABLE_ADDR, 1);
    write_mexp_reg(s2, MEMEXPOSE_INTR_ENABLE_ADDR, 1);
}

static void test_memexpose_simple_memshare(void)
{
    size_t sixty_four_megs = 1 << (20 + 6);
    uint32_t in, out;

    MexpState s1, s2;
    setup_connected_vms(&s1, &s2);

    in = 0xdeadbeef;
    write_mem(&s1, sixty_four_megs, &in, 4);
    read_mexp_mem(&s2, sixty_four_megs, &out, 4);
    g_assert_cmphex(in, ==, out);
    in = 0xbaba1510;
    write_mem(&s1, sixty_four_megs, &in, 4);
    read_mexp_mem(&s2, sixty_four_megs, &out, 4);
    g_assert_cmphex(in, ==, out);

    in = 0xaaaaaaaa;
    write_mexp_mem(&s1, sixty_four_megs, &in, 4);
    read_mem(&s2, sixty_four_megs, &out, 4);
    g_assert_cmphex(in, ==, out);
    in = 0xbbbbbbbb;
    write_mexp_mem(&s1, sixty_four_megs, &in, 4);
    read_mem(&s2, sixty_four_megs, &out, 4);
    g_assert_cmphex(in, ==, out);

    cleanup_vm(&s1);
    cleanup_vm(&s2);
}

static void test_memexpose_simple_interrupts(void)
{
    MexpState s1, s2;
    setup_connected_vms(&s1, &s2);

    mexp_send_intr(&s1, 0x1, 0xdeadbea7);
    mexp_send_intr(&s1, 0x2, 0xdeadbaba);

    uint64_t type, data, received;

    received = mexp_recv_intr(&s2, &type, &data);
    g_assert_cmpuint(received, ==, 1);
    g_assert_cmphex(type, ==, 0x1);
    g_assert_cmphex(data, ==, 0xdeadbea7);

    received = mexp_recv_intr(&s2, &type, &data);
    g_assert_cmpuint(received, ==, 1);
    g_assert_cmphex(type, ==, 0x2);
    g_assert_cmphex(data, ==, 0xdeadbaba);

    cleanup_vm(&s1);
    cleanup_vm(&s2);
}

static void test_memexpose_overfull_intr_queue(void)
{
    MexpState s1, s2;
    setup_connected_vms(&s1, &s2);

    unsigned int i, expected, runs = MEMEXPOSE_INTR_QUEUE_SIZE + 10;
    uint64_t type, data;

    for (i = 0; i < runs; i++) {
        mexp_send_intr(&s1, i, i);
    }

    expected = 0;
    while (mexp_recv_intr(&s2, &type, &data)) {
        if (expected < MEMEXPOSE_INTR_QUEUE_SIZE) {
            g_assert_cmphex(type, ==, expected);
            g_assert_cmphex(data, ==, expected);
            expected += 1;
        } else {
            g_assert_cmphex(type, >, expected);
            g_assert_cmphex(type, <, runs);
            g_assert_cmphex(data, >, expected);
            g_assert_cmphex(data, <, runs);
            expected = type;
        }
    }
    g_assert_cmpuint(expected, >=, MEMEXPOSE_INTR_QUEUE_SIZE - 1);

    cleanup_vm(&s1);
    cleanup_vm(&s2);
}

static void test_memexpose_intr_data(void)
{
    MexpState s1, s2;
    setup_connected_vms(&s1, &s2);

    unsigned int i;
    uint64_t type, data, received;

    uint64_t send = 1;
    write_mexp_reg(&s1, MEMEXPOSE_INTR_TX_TYPE_ADDR, 0);
    for (i = 0; i < MEMEXPOSE_MAX_INTR_DATA_SIZE; i += 8) {
        write_mexp_reg(&s1, MEMEXPOSE_INTR_TX_DATA_ADDR + i, i);
    }
    write_mexp_reg(&s1, MEMEXPOSE_INTR_SEND_ADDR, send);

    received = mexp_recv_intr(&s2, &type, &data);
    g_assert_cmpuint(received, ==, 1);
    for (i = 0; i < MEMEXPOSE_MAX_INTR_DATA_SIZE; i += 8) {
        data = read_mexp_reg(&s1, MEMEXPOSE_INTR_TX_DATA_ADDR + i);
        g_assert_cmphex(data, ==, i);
    }

    cleanup_vm(&s1);
    cleanup_vm(&s2);
}

static void cleanup(void)
{
    if (tmpshm) {
        g_rmdir(tmpshm);
        tmpshm = NULL;
    }

    if (tmpdir) {
        remove_socks(tmpdir);
        g_rmdir(tmpdir);
        tmpdir = NULL;
    }
}

static void abrt_handler(void *data)
{
    cleanup();
}

int main(int argc, char **argv)
{
    int ret;
    gchar dir[] = "/tmp/memexpose-test.XXXXXX";
    gchar shmdir[] = "/dev/shm/memexpose-test.XXXXXX";

    g_test_init(&argc, &argv, NULL);

    qtest_add_abrt_handler(abrt_handler, NULL);

    if (mkdtemp(dir) == NULL) {
        g_error("mkdtemp: %s", g_strerror(errno));
        goto out;
    }
    tmpdir = dir;
    if (mkdtemp(shmdir) == NULL) {
        g_error("mkdtemp: %s", g_strerror(errno));
        goto out;
    }
    tmpshm = shmdir;

    qtest_add_func("/memexpose/memory", test_memexpose_simple_memshare);
    qtest_add_func("/memexpose/interrupts", test_memexpose_simple_interrupts);
    qtest_add_func("/memexpose/interrupts_full_queue",
                   test_memexpose_overfull_intr_queue);
    qtest_add_func("/memexpose/interrupts_all_data", test_memexpose_intr_data);
    ret = g_test_run();

out:
    cleanup();
    return ret;
}
