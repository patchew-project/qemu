/*
 * QTest testcase for TCG instruction counting.
 *
 * Copyright Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define AP_VECTOR       0x01
#define DONE_ADDR       0x50c
#define TSC0_ADDR       0x504
#define TSC1_ADDR       0x508
#define ITER_ADDR       0x510
#define AP_FLAG_ADDR    0x600

/*
 * The BSP reads the TSC, sends INIT and SIPI to the AP and spins until
 * the AP sets a flag, counting iterations.  The AP then halts.
 */
static const uint8_t x86_init_sector[] = {
    /* mov $0, %ax; mov %ax, %ds */
    0xb8, 0x00, 0x00, 0x8e, 0xd8,
    /* AP code at AP_VECTOR << 12: movb $1, flag; cli; 1: hlt; jmp 1b */
    0xc7, 0x06, 0x00, 0x10, 0xc6, 0x06,
    0xc7, 0x06, 0x02, 0x10, AP_FLAG_ADDR & 0xff, AP_FLAG_ADDR >> 8,
    0xc7, 0x06, 0x04, 0x10, 0x01, 0xfa,
    0xc7, 0x06, 0x06, 0x10, 0xf4, 0xeb,
    0xc6, 0x06, 0x08, 0x10, 0xfd,
    /* mov $MSR_IA32_APICBASE, %ecx; rdmsr; or $EXTD, %eax; wrmsr */
    0x66, 0xb9, 0x1b, 0x00, 0x00, 0x00, 0x0f, 0x32,
    0x66, 0x0d, 0x00, 0x04, 0x00, 0x00, 0x0f, 0x30,
    /* xor %esi, %esi; rdtsc; mov %eax, tsc0 */
    0x66, 0x31, 0xf6,
    0x0f, 0x31, 0x66, 0xa3, TSC0_ADDR & 0xff, TSC0_ADDR >> 8,
    /* mov $ICR, %ecx; mov $1, %edx; INIT, then SIPI */
    0x66, 0xb9, 0x30, 0x08, 0x00, 0x00,
    0x66, 0xba, 0x01, 0x00, 0x00, 0x00,
    0x66, 0xb8, 0x00, 0x45, 0x00, 0x00, 0x0f, 0x30,
    0x66, 0xb8, AP_VECTOR, 0x46, 0x00, 0x00, 0x0f, 0x30,
    /* 1: inc %esi; cmpb $0, flag; je 1b: three instructions */
    0x66, 0x46,
    0x80, 0x3e, AP_FLAG_ADDR & 0xff, AP_FLAG_ADDR >> 8, 0x00,
    0x74, 0xf7,
    /* rdtsc; mov %eax, tsc1; mov %esi, iterations; done = 1 */
    0x0f, 0x31, 0x66, 0xa3, TSC1_ADDR & 0xff, TSC1_ADDR >> 8,
    0x66, 0x89, 0x36, ITER_ADDR & 0xff, ITER_ADDR >> 8,
    0xc6, 0x06, DONE_ADDR & 0xff, DONE_ADDR >> 8, 0x01,
    /* cli; 1: hlt; jmp 1b */
    0xfa, 0xf4, 0xeb, 0xfd,
};

static char *write_boot_disk(void)
{
    size_t image_size = 0x7e000;
    g_autofree uint8_t *image = g_malloc0(image_size);
    char *path = g_strdup("/tmp/qemu-icount-XXXXXX");
    ssize_t written;
    int fd;

    memcpy(image, x86_init_sector, sizeof(x86_init_sector));
    image[0x1fe] = 0x55;
    image[0x1ff] = 0xaa;

    fd = mkstemp(path);
    g_assert_cmpint(fd, >=, 0);
    written = write(fd, image, image_size);
    close(fd);
    g_assert_cmpint(written, ==, image_size);
    return path;
}

static void wait_for_done(QTestState *qts)
{
    for (unsigned i = 0; i < 5000; i++) {
        if (qtest_readb(qts, DONE_ADDR)) {
            return;
        }
        g_usleep(1000);
    }
    g_assert_not_reached();
}

static void test_x86_pc_x2apic_fdc(void)
{
    g_autofree char *disk = write_boot_disk();
    QTestState *qts;
    uint32_t iterations;
    uint32_t delta;

    qts = qtest_initf("-M pc -m 16M -smp 2 -accel tcg -icount shift=0 "
                      "-cpu max,x2apic=on,tsc-frequency=1000000000 "
                      "-drive if=floppy,format=raw,file=%s", disk);
    wait_for_done(qts);

    delta = qtest_readl(qts, TSC1_ADDR) - qtest_readl(qts, TSC0_ADDR);
    iterations = qtest_readl(qts, ITER_ADDR);
    g_test_message("TSC delta %u, %u iterations", delta, iterations);
    g_assert_cmpuint(iterations, >, 0);
    g_assert_cmpuint(delta, >=, 3 * iterations);
    g_assert_cmpuint(delta, <=, 3 * iterations + 32);

    qtest_quit(qts);
    unlink(disk);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_accel("tcg") && qtest_has_machine("pc")) {
        qtest_add_func("/icount/x86-pc-x2apic-fdc",
                       test_x86_pc_x2apic_fdc);
    }

    return g_test_run();
}
