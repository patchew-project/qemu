/*
 * QEMU memory region access test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Author: Tomoyuki HIROSE <hrstmyk811m@gmail.com>
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#include "hw/misc/memaccess-testdev.h"

static const char *arch = "";
static const hwaddr base = 0x200000000;

struct arch2cpu {
    const char *arch;
    const char *cpu_model;
};

static struct arch2cpu cpus_map[] = {
    /* tested targets list */
    { "arm", "cortex-a15" },
    { "aarch64", "cortex-a57" },
    { "avr", "avr6-avr-cpu" },
    { "x86_64", "qemu64,apic-id=0" },
    { "i386", "qemu32,apic-id=0" },
    { "alpha", "ev67" },
    { "cris", "crisv32" },
    { "m68k", "m5206" },
    { "microblaze", "any" },
    { "microblazeel", "any" },
    { "mips", "4Kc" },
    { "mipsel", "I7200" },
    { "mips64", "20Kc" },
    { "mips64el", "I6500" },
    { "or1k", "or1200" },
    { "ppc", "604" },
    { "ppc64", "power8e_v2.1" },
    { "s390x", "qemu" },
    { "sh4", "sh7750r" },
    { "sh4eb", "sh7751r" },
    { "sparc", "LEON2" },
    { "sparc64", "Fujitsu Sparc64" },
    { "tricore", "tc1796" },
    { "xtensa", "dc233c" },
    { "xtensaeb", "fsf" },
    { "hppa", "hppa" },
    { "riscv64", "rv64" },
    { "riscv32", "rv32" },
    { "rx", "rx62n" },
    { "loongarch64", "la464" },
};

static const char *get_cpu_model_by_arch(const char *arch)
{
    for (int i = 0; i < ARRAY_SIZE(cpus_map); i++) {
        if (!strcmp(arch, cpus_map[i].arch)) {
            return cpus_map[i].cpu_model;
        }
    }
    return NULL;
}

static QTestState *create_memaccess_qtest(void)
{
    QTestState *qts;

    qts = qtest_initf("-machine none -cpu \"%s\" "
                      "-device memaccess-testdev,address=0x%" PRIx64,
                      get_cpu_model_by_arch(arch), base);
    return qts;
}

static void little_b_valid(QTestState *qts, uint64_t offset)
{
    qtest_writeb(qts, base + offset + 0, 0x00);
    qtest_writeb(qts, base + offset + 1, 0x11);
    qtest_writeb(qts, base + offset + 2, 0x22);
    qtest_writeb(qts, base + offset + 3, 0x33);
    qtest_writeb(qts, base + offset + 4, 0x44);
    qtest_writeb(qts, base + offset + 5, 0x55);
    qtest_writeb(qts, base + offset + 6, 0x66);
    qtest_writeb(qts, base + offset + 7, 0x77);
    g_assert_cmphex(qtest_readb(qts, base + offset + 0), ==, 0x00);
    g_assert_cmphex(qtest_readb(qts, base + offset + 1), ==, 0x11);
    g_assert_cmphex(qtest_readb(qts, base + offset + 2), ==, 0x22);
    g_assert_cmphex(qtest_readb(qts, base + offset + 3), ==, 0x33);
    g_assert_cmphex(qtest_readb(qts, base + offset + 4), ==, 0x44);
    g_assert_cmphex(qtest_readb(qts, base + offset + 5), ==, 0x55);
    g_assert_cmphex(qtest_readb(qts, base + offset + 6), ==, 0x66);
    g_assert_cmphex(qtest_readb(qts, base + offset + 7), ==, 0x77);
}

static void little_b_invalid(QTestState *qts, uint64_t offset)
{
    qtest_writeb(qts, base + offset + 0, 0x00);
    qtest_writeb(qts, base + offset + 1, 0x11);
    qtest_writeb(qts, base + offset + 2, 0x22);
    qtest_writeb(qts, base + offset + 3, 0x33);
    qtest_writeb(qts, base + offset + 4, 0x44);
    qtest_writeb(qts, base + offset + 5, 0x55);
    qtest_writeb(qts, base + offset + 6, 0x66);
    qtest_writeb(qts, base + offset + 7, 0x77);
    g_assert_cmphex(qtest_readb(qts, base + offset + 0), ==, 0x00);
    g_assert_cmphex(qtest_readb(qts, base + offset + 1), ==, 0x11);
    g_assert_cmphex(qtest_readb(qts, base + offset + 2), ==, 0x22);
    g_assert_cmphex(qtest_readb(qts, base + offset + 3), ==, 0x33);
    g_assert_cmphex(qtest_readb(qts, base + offset + 4), ==, 0x44);
    g_assert_cmphex(qtest_readb(qts, base + offset + 5), ==, 0x55);
    g_assert_cmphex(qtest_readb(qts, base + offset + 6), ==, 0x66);
    g_assert_cmphex(qtest_readb(qts, base + offset + 7), ==, 0x77);
}

static void little_w_valid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 1, 0x3322);
        qtest_writew(qts, base + offset + 2, 0x5544);
        qtest_writew(qts, base + offset + 3, 0x7766);
        qtest_writew(qts, base + offset + 4, 0x9988);
        qtest_writew(qts, base + offset + 5, 0xbbaa);
        qtest_writew(qts, base + offset + 6, 0xddcc);
        qtest_writew(qts, base + offset + 7, 0xffee);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x1133);
        g_assert_cmphex(qtest_readw(qts, base + offset + 1), ==, 0x3355);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x5577);
        g_assert_cmphex(qtest_readw(qts, base + offset + 3), ==, 0x7799);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0x99bb);
        g_assert_cmphex(qtest_readw(qts, base + offset + 5), ==, 0xbbdd);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0xddff);
        g_assert_cmphex(qtest_readw(qts, base + offset + 7), ==, 0xffee);
    } else {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 1, 0x3322);
        qtest_writew(qts, base + offset + 2, 0x5544);
        qtest_writew(qts, base + offset + 3, 0x7766);
        qtest_writew(qts, base + offset + 4, 0x9988);
        qtest_writew(qts, base + offset + 5, 0xbbaa);
        qtest_writew(qts, base + offset + 6, 0xddcc);
        qtest_writew(qts, base + offset + 7, 0xffee);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x2200);
        g_assert_cmphex(qtest_readw(qts, base + offset + 1), ==, 0x4422);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x6644);
        g_assert_cmphex(qtest_readw(qts, base + offset + 3), ==, 0x8866);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0xaa88);
        g_assert_cmphex(qtest_readw(qts, base + offset + 5), ==, 0xccaa);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0xeecc);
        g_assert_cmphex(qtest_readw(qts, base + offset + 7), ==, 0xffee);
    }
}

static void little_w_invalid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 2, 0x3322);
        qtest_writew(qts, base + offset + 4, 0x5544);
        qtest_writew(qts, base + offset + 6, 0x7766);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x1100);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x3322);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0x5544);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0x7766);
    } else {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 2, 0x3322);
        qtest_writew(qts, base + offset + 4, 0x5544);
        qtest_writew(qts, base + offset + 6, 0x7766);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x1100);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x3322);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0x5544);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0x7766);
    }
}

static void little_l_valid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 1, 0x77665544);
        qtest_writel(qts, base + offset + 2, 0xbbaa9988);
        qtest_writel(qts, base + offset + 3, 0xffeeddcc);
        qtest_writel(qts, base + offset + 4, 0x01234567);
        qtest_writel(qts, base + offset + 5, 0x89abcdef);
        qtest_writel(qts, base + offset + 6, 0xfedcba98);
        qtest_writel(qts, base + offset + 7, 0x76543210);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0x3377bbff);
        g_assert_cmphex(qtest_readl(qts, base + offset + 1), ==, 0x77bbff01);
        g_assert_cmphex(qtest_readl(qts, base + offset + 2), ==, 0xbbff0189);
        g_assert_cmphex(qtest_readl(qts, base + offset + 3), ==, 0xff0189fe);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x0189fe76);
        g_assert_cmphex(qtest_readl(qts, base + offset + 5), ==, 0x89fe7654);
        g_assert_cmphex(qtest_readl(qts, base + offset + 6), ==, 0xfe765432);
        g_assert_cmphex(qtest_readl(qts, base + offset + 7), ==, 0x76543210);
    } else {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 1, 0x77665544);
        qtest_writel(qts, base + offset + 2, 0xbbaa9988);
        qtest_writel(qts, base + offset + 3, 0xffeeddcc);
        qtest_writel(qts, base + offset + 4, 0x01234567);
        qtest_writel(qts, base + offset + 5, 0x89abcdef);
        qtest_writel(qts, base + offset + 6, 0xfedcba98);
        qtest_writel(qts, base + offset + 7, 0x76543210);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0xcc884400);
        g_assert_cmphex(qtest_readl(qts, base + offset + 1), ==, 0x67cc8844);
        g_assert_cmphex(qtest_readl(qts, base + offset + 2), ==, 0xef67cc88);
        g_assert_cmphex(qtest_readl(qts, base + offset + 3), ==, 0x98ef67cc);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x1098ef67);
        g_assert_cmphex(qtest_readl(qts, base + offset + 5), ==, 0x321098ef);
        g_assert_cmphex(qtest_readl(qts, base + offset + 6), ==, 0x54321098);
        g_assert_cmphex(qtest_readl(qts, base + offset + 7), ==, 0x76543210);
    }
}

static void little_l_invalid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 4, 0x77665544);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0x33221100);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x77665544);
    } else {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 4, 0x77665544);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0x33221100);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x77665544);
    }
}

static void little_q_valid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        qtest_writeq(qts, base + offset + 1, 0xffeeddccbbaa9988);
        qtest_writeq(qts, base + offset + 2, 0xfedcba9876543210);
        qtest_writeq(qts, base + offset + 3, 0x0123456789abcdef);
        qtest_writeq(qts, base + offset + 4, 0xdeadbeefdeadbeef);
        qtest_writeq(qts, base + offset + 5, 0xcafebabecafebabe);
        qtest_writeq(qts, base + offset + 6, 0xbeefcafebeefcafe);
        qtest_writeq(qts, base + offset + 7, 0xfacefeedfacefeed);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0x77fffe01decabefa);
        g_assert_cmphex(qtest_readq(qts, base + offset + 1), ==,
                        0xfffe01decabeface);
        g_assert_cmphex(qtest_readq(qts, base + offset + 2), ==,
                        0xfe01decabefacefe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 3), ==,
                        0x01decabefacefeed);
        g_assert_cmphex(qtest_readq(qts, base + offset + 4), ==,
                        0xdecabefacefeedfa);
        g_assert_cmphex(qtest_readq(qts, base + offset + 5), ==,
                        0xcabefacefeedface);
        g_assert_cmphex(qtest_readq(qts, base + offset + 6), ==,
                        0xbefacefeedfacefe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 7), ==,
                        0xfacefeedfacefeed);
    } else {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        qtest_writeq(qts, base + offset + 1, 0xffeeddccbbaa9988);
        qtest_writeq(qts, base + offset + 2, 0xfedcba9876543210);
        qtest_writeq(qts, base + offset + 3, 0x0123456789abcdef);
        qtest_writeq(qts, base + offset + 4, 0xdeadbeefdeadbeef);
        qtest_writeq(qts, base + offset + 5, 0xcafebabecafebabe);
        qtest_writeq(qts, base + offset + 6, 0xbeefcafebeefcafe);
        qtest_writeq(qts, base + offset + 7, 0xfacefeedfacefeed);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0xedfebeefef108800);
        g_assert_cmphex(qtest_readq(qts, base + offset + 1), ==,
                        0xfeedfebeefef1088);
        g_assert_cmphex(qtest_readq(qts, base + offset + 2), ==,
                        0xcefeedfebeefef10);
        g_assert_cmphex(qtest_readq(qts, base + offset + 3), ==,
                        0xfacefeedfebeefef);
        g_assert_cmphex(qtest_readq(qts, base + offset + 4), ==,
                        0xedfacefeedfebeef);
        g_assert_cmphex(qtest_readq(qts, base + offset + 5), ==,
                        0xfeedfacefeedfebe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 6), ==,
                        0xcefeedfacefeedfe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 7), ==,
                        0xfacefeedfacefeed);
    }
}

static void little_q_invalid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0x7766554433221100);
    } else {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0x7766554433221100);
    }
}

static void big_b_valid(QTestState *qts, uint64_t offset)
{
    qtest_writeb(qts, base + offset + 0, 0x00);
    qtest_writeb(qts, base + offset + 1, 0x11);
    qtest_writeb(qts, base + offset + 2, 0x22);
    qtest_writeb(qts, base + offset + 3, 0x33);
    qtest_writeb(qts, base + offset + 4, 0x44);
    qtest_writeb(qts, base + offset + 5, 0x55);
    qtest_writeb(qts, base + offset + 6, 0x66);
    qtest_writeb(qts, base + offset + 7, 0x77);
    g_assert_cmphex(qtest_readb(qts, base + offset + 0), ==, 0x00);
    g_assert_cmphex(qtest_readb(qts, base + offset + 1), ==, 0x11);
    g_assert_cmphex(qtest_readb(qts, base + offset + 2), ==, 0x22);
    g_assert_cmphex(qtest_readb(qts, base + offset + 3), ==, 0x33);
    g_assert_cmphex(qtest_readb(qts, base + offset + 4), ==, 0x44);
    g_assert_cmphex(qtest_readb(qts, base + offset + 5), ==, 0x55);
    g_assert_cmphex(qtest_readb(qts, base + offset + 6), ==, 0x66);
    g_assert_cmphex(qtest_readb(qts, base + offset + 7), ==, 0x77);
}

static void big_b_invalid(QTestState *qts, uint64_t offset)
{
    qtest_writeb(qts, base + offset + 0, 0x00);
    qtest_writeb(qts, base + offset + 1, 0x11);
    qtest_writeb(qts, base + offset + 2, 0x22);
    qtest_writeb(qts, base + offset + 3, 0x33);
    qtest_writeb(qts, base + offset + 4, 0x44);
    qtest_writeb(qts, base + offset + 5, 0x55);
    qtest_writeb(qts, base + offset + 6, 0x66);
    qtest_writeb(qts, base + offset + 7, 0x77);
    g_assert_cmphex(qtest_readb(qts, base + offset + 0), ==, 0x00);
    g_assert_cmphex(qtest_readb(qts, base + offset + 1), ==, 0x11);
    g_assert_cmphex(qtest_readb(qts, base + offset + 2), ==, 0x22);
    g_assert_cmphex(qtest_readb(qts, base + offset + 3), ==, 0x33);
    g_assert_cmphex(qtest_readb(qts, base + offset + 4), ==, 0x44);
    g_assert_cmphex(qtest_readb(qts, base + offset + 5), ==, 0x55);
    g_assert_cmphex(qtest_readb(qts, base + offset + 6), ==, 0x66);
    g_assert_cmphex(qtest_readb(qts, base + offset + 7), ==, 0x77);
}

static void big_w_valid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 1, 0x3322);
        qtest_writew(qts, base + offset + 2, 0x5544);
        qtest_writew(qts, base + offset + 3, 0x7766);
        qtest_writew(qts, base + offset + 4, 0x9988);
        qtest_writew(qts, base + offset + 5, 0xbbaa);
        qtest_writew(qts, base + offset + 6, 0xddcc);
        qtest_writew(qts, base + offset + 7, 0xffee);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x1133);
        g_assert_cmphex(qtest_readw(qts, base + offset + 1), ==, 0x3355);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x5577);
        g_assert_cmphex(qtest_readw(qts, base + offset + 3), ==, 0x7799);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0x99bb);
        g_assert_cmphex(qtest_readw(qts, base + offset + 5), ==, 0xbbdd);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0xddff);
        g_assert_cmphex(qtest_readw(qts, base + offset + 7), ==, 0xffee);
    } else {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 1, 0x3322);
        qtest_writew(qts, base + offset + 2, 0x5544);
        qtest_writew(qts, base + offset + 3, 0x7766);
        qtest_writew(qts, base + offset + 4, 0x9988);
        qtest_writew(qts, base + offset + 5, 0xbbaa);
        qtest_writew(qts, base + offset + 6, 0xddcc);
        qtest_writew(qts, base + offset + 7, 0xffee);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x2200);
        g_assert_cmphex(qtest_readw(qts, base + offset + 1), ==, 0x4422);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x6644);
        g_assert_cmphex(qtest_readw(qts, base + offset + 3), ==, 0x8866);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0xaa88);
        g_assert_cmphex(qtest_readw(qts, base + offset + 5), ==, 0xccaa);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0xeecc);
        g_assert_cmphex(qtest_readw(qts, base + offset + 7), ==, 0xffee);
    }
}

static void big_w_invalid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 2, 0x3322);
        qtest_writew(qts, base + offset + 4, 0x5544);
        qtest_writew(qts, base + offset + 6, 0x7766);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x1100);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x3322);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0x5544);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0x7766);
    } else {
        qtest_writew(qts, base + offset + 0, 0x1100);
        qtest_writew(qts, base + offset + 2, 0x3322);
        qtest_writew(qts, base + offset + 4, 0x5544);
        qtest_writew(qts, base + offset + 6, 0x7766);
        g_assert_cmphex(qtest_readw(qts, base + offset + 0), ==, 0x1100);
        g_assert_cmphex(qtest_readw(qts, base + offset + 2), ==, 0x3322);
        g_assert_cmphex(qtest_readw(qts, base + offset + 4), ==, 0x5544);
        g_assert_cmphex(qtest_readw(qts, base + offset + 6), ==, 0x7766);
    }
}

static void big_l_valid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 1, 0x77665544);
        qtest_writel(qts, base + offset + 2, 0xbbaa9988);
        qtest_writel(qts, base + offset + 3, 0xffeeddcc);
        qtest_writel(qts, base + offset + 4, 0x01234567);
        qtest_writel(qts, base + offset + 5, 0x89abcdef);
        qtest_writel(qts, base + offset + 6, 0xfedcba98);
        qtest_writel(qts, base + offset + 7, 0x76543210);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0x3377bbff);
        g_assert_cmphex(qtest_readl(qts, base + offset + 1), ==, 0x77bbff01);
        g_assert_cmphex(qtest_readl(qts, base + offset + 2), ==, 0xbbff0189);
        g_assert_cmphex(qtest_readl(qts, base + offset + 3), ==, 0xff0189fe);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x0189fe76);
        g_assert_cmphex(qtest_readl(qts, base + offset + 5), ==, 0x89fe7654);
        g_assert_cmphex(qtest_readl(qts, base + offset + 6), ==, 0xfe765432);
        g_assert_cmphex(qtest_readl(qts, base + offset + 7), ==, 0x76543210);
    } else {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 1, 0x77665544);
        qtest_writel(qts, base + offset + 2, 0xbbaa9988);
        qtest_writel(qts, base + offset + 3, 0xffeeddcc);
        qtest_writel(qts, base + offset + 4, 0x01234567);
        qtest_writel(qts, base + offset + 5, 0x89abcdef);
        qtest_writel(qts, base + offset + 6, 0xfedcba98);
        qtest_writel(qts, base + offset + 7, 0x76543210);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0xcc884400);
        g_assert_cmphex(qtest_readl(qts, base + offset + 1), ==, 0x67cc8844);
        g_assert_cmphex(qtest_readl(qts, base + offset + 2), ==, 0xef67cc88);
        g_assert_cmphex(qtest_readl(qts, base + offset + 3), ==, 0x98ef67cc);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x1098ef67);
        g_assert_cmphex(qtest_readl(qts, base + offset + 5), ==, 0x321098ef);
        g_assert_cmphex(qtest_readl(qts, base + offset + 6), ==, 0x54321098);
        g_assert_cmphex(qtest_readl(qts, base + offset + 7), ==, 0x76543210);
    }
}

static void big_l_invalid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 4, 0x77665544);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0x33221100);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x77665544);
    } else {
        qtest_writel(qts, base + offset + 0, 0x33221100);
        qtest_writel(qts, base + offset + 4, 0x77665544);
        g_assert_cmphex(qtest_readl(qts, base + offset + 0), ==, 0x33221100);
        g_assert_cmphex(qtest_readl(qts, base + offset + 4), ==, 0x77665544);
    }
}

static void big_q_valid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        qtest_writeq(qts, base + offset + 1, 0xffeeddccbbaa9988);
        qtest_writeq(qts, base + offset + 2, 0xfedcba9876543210);
        qtest_writeq(qts, base + offset + 3, 0x0123456789abcdef);
        qtest_writeq(qts, base + offset + 4, 0xdeadbeefdeadbeef);
        qtest_writeq(qts, base + offset + 5, 0xcafebabecafebabe);
        qtest_writeq(qts, base + offset + 6, 0xbeefcafebeefcafe);
        qtest_writeq(qts, base + offset + 7, 0xfacefeedfacefeed);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0x77fffe01decabefa);
        g_assert_cmphex(qtest_readq(qts, base + offset + 1), ==,
                        0xfffe01decabeface);
        g_assert_cmphex(qtest_readq(qts, base + offset + 2), ==,
                        0xfe01decabefacefe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 3), ==,
                        0x01decabefacefeed);
        g_assert_cmphex(qtest_readq(qts, base + offset + 4), ==,
                        0xdecabefacefeedfa);
        g_assert_cmphex(qtest_readq(qts, base + offset + 5), ==,
                        0xcabefacefeedface);
        g_assert_cmphex(qtest_readq(qts, base + offset + 6), ==,
                        0xbefacefeedfacefe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 7), ==,
                        0xfacefeedfacefeed);
    } else {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        qtest_writeq(qts, base + offset + 1, 0xffeeddccbbaa9988);
        qtest_writeq(qts, base + offset + 2, 0xfedcba9876543210);
        qtest_writeq(qts, base + offset + 3, 0x0123456789abcdef);
        qtest_writeq(qts, base + offset + 4, 0xdeadbeefdeadbeef);
        qtest_writeq(qts, base + offset + 5, 0xcafebabecafebabe);
        qtest_writeq(qts, base + offset + 6, 0xbeefcafebeefcafe);
        qtest_writeq(qts, base + offset + 7, 0xfacefeedfacefeed);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0xedfebeefef108800);
        g_assert_cmphex(qtest_readq(qts, base + offset + 1), ==,
                        0xfeedfebeefef1088);
        g_assert_cmphex(qtest_readq(qts, base + offset + 2), ==,
                        0xcefeedfebeefef10);
        g_assert_cmphex(qtest_readq(qts, base + offset + 3), ==,
                        0xfacefeedfebeefef);
        g_assert_cmphex(qtest_readq(qts, base + offset + 4), ==,
                        0xedfacefeedfebeef);
        g_assert_cmphex(qtest_readq(qts, base + offset + 5), ==,
                        0xfeedfacefeedfebe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 6), ==,
                        0xcefeedfacefeedfe);
        g_assert_cmphex(qtest_readq(qts, base + offset + 7), ==,
                        0xfacefeedfacefeed);
    }
}

static void big_q_invalid(QTestState *qts, hwaddr offset)
{
    if (qtest_big_endian(qts)) {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0x7766554433221100);
    } else {
        qtest_writeq(qts, base + offset + 0, 0x7766554433221100);
        g_assert_cmphex(qtest_readq(qts, base + offset + 0), ==,
                        0x7766554433221100);
    }
}

#define DEFINE_test_memaccess(e, e_u, w, w_u, v, v_u)                   \
    static void                                                         \
    test_memaccess_##e##_##w##_##v(void)                                \
    {                                                                   \
        QTestState *qts;                                                \
        qts = create_memaccess_qtest();                                 \
        if (!qts) {                                                     \
            return;                                                     \
        }                                                               \
                                                                        \
        for (size_t i = OFF_IDX_OPS_LIST_##e_u##_##w_u##_##v_u;         \
             i < OFF_IDX_OPS_LIST_##e_u##_##w_u##_##v_u +               \
                 N_OPS_LIST_##e_u##_##w_u##_##v_u;                      \
             i++) {                                                     \
            e##_##w##_##v(qts, MEMACCESS_TESTDEV_REGION_SIZE * i);      \
        }                                                               \
                                                                        \
        qtest_quit(qts);                                                \
    }

DEFINE_test_memaccess(little, LITTLE, b, B, valid, VALID)
DEFINE_test_memaccess(little, LITTLE, w, W, valid, VALID)
DEFINE_test_memaccess(little, LITTLE, l, L, valid, VALID)
DEFINE_test_memaccess(little, LITTLE, q, Q, valid, VALID)
DEFINE_test_memaccess(little, LITTLE, b, B, invalid, INVALID)
DEFINE_test_memaccess(little, LITTLE, w, W, invalid, INVALID)
DEFINE_test_memaccess(little, LITTLE, l, L, invalid, INVALID)
DEFINE_test_memaccess(little, LITTLE, q, Q, invalid, INVALID)
DEFINE_test_memaccess(big, BIG, b, B, valid, VALID)
DEFINE_test_memaccess(big, BIG, w, W, valid, VALID)
DEFINE_test_memaccess(big, BIG, l, L, valid, VALID)
DEFINE_test_memaccess(big, BIG, q, Q, valid, VALID)
DEFINE_test_memaccess(big, BIG, b, B, invalid, INVALID)
DEFINE_test_memaccess(big, BIG, w, W, invalid, INVALID)
DEFINE_test_memaccess(big, BIG, l, L, invalid, INVALID)
DEFINE_test_memaccess(big, BIG, q, Q, invalid, INVALID)

#undef DEFINE_test_memaccess

static struct {
    const char *name;
    void (*test)(void);
} tests[] = {
    {"little_b_valid", test_memaccess_little_b_valid},
    {"little_w_valid", test_memaccess_little_w_valid},
    {"little_l_valid", test_memaccess_little_l_valid},
    {"little_q_valid", test_memaccess_little_q_valid},
    {"little_b_invalid", test_memaccess_little_b_invalid},
    {"little_w_invalid", test_memaccess_little_w_invalid},
    {"little_l_invalid", test_memaccess_little_l_invalid},
    {"little_q_invalid", test_memaccess_little_q_invalid},
    {"big_b_valid", test_memaccess_big_b_valid},
    {"big_w_valid", test_memaccess_big_w_valid},
    {"big_l_valid", test_memaccess_big_l_valid},
    {"big_q_valid", test_memaccess_big_q_valid},
    {"big_b_invalid", test_memaccess_big_b_invalid},
    {"big_w_invalid", test_memaccess_big_w_invalid},
    {"big_l_invalid", test_memaccess_big_l_invalid},
    {"big_q_invalid", test_memaccess_big_q_invalid},
};

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    arch = qtest_get_arch();

    for (int i = 0; i < ARRAY_SIZE(tests); i++) {
        g_autofree gchar *path = g_strdup_printf("memaccess/%s", tests[i].name);
        qtest_add_func(path, tests[i].test);
    }

    return g_test_run();
}
