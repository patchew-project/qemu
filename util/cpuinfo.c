/*
 * Dealing with arm cpu identification information.
 *
 * Copyright (C) 2016 Cavium, Inc.
 *
 * Authors:
 *  Vijaya Kumar K <Vijaya.Kumar@cavium.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"

#if defined(__aarch64__)

#define MIDR_IMPLEMENTER_SHIFT  24
#define MIDR_IMPLEMENTER_MASK   (0xffULL << MIDR_IMPLEMENTER_SHIFT)
#define MIDR_ARCHITECTURE_SHIFT 16
#define MIDR_ARCHITECTURE_MASK  (0xf << MIDR_ARCHITECTURE_SHIFT)
#define MIDR_PARTNUM_SHIFT      4
#define MIDR_PARTNUM_MASK       (0xfff << MIDR_PARTNUM_SHIFT)

#define MIDR_CPU_PART(imp, partnum) \
        (((imp)                 << MIDR_IMPLEMENTER_SHIFT)  | \
        (0xf                    << MIDR_ARCHITECTURE_SHIFT) | \
        ((partnum)              << MIDR_PARTNUM_SHIFT))

#define ARM_CPU_IMP_CAVIUM        0x43
#define CAVIUM_CPU_PART_THUNDERX  0x0A1

#define MIDR_THUNDERX  \
               MIDR_CPU_PART(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX)
#define CPU_MODEL_MASK  (MIDR_IMPLEMENTER_MASK | MIDR_ARCHITECTURE_MASK | \
                         MIDR_PARTNUM_MASK)

long int qemu_read_cpuid_info(void)
{
    FILE *fp;
    char *buf;
    long int midr = 0;
#define BUF_SIZE 32

    fp = fopen("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1",
               "r");
    if (!fp) {
        return 0;
    }

    buf = g_malloc0(BUF_SIZE);
    if (!buf) {
        fclose(fp);
        return 0;
    }

    if (buf != fgets(buf, BUF_SIZE - 1, fp)) {
        goto out;
    }

    if (qemu_strtol(buf, NULL, 0, &midr) < 0) {
        goto out;
    }

out:
    g_free(buf);
    fclose(fp);

    return midr;
}

bool is_thunder_pass2_cpu(void)
{
    static bool cpu_info_read;
    static long int midr_thunder_val;

    if (!cpu_info_read) {
        midr_thunder_val = qemu_read_cpuid_info();
        midr_thunder_val &= CPU_MODEL_MASK;
        cpu_info_read = 1;
    }

    if (midr_thunder_val == MIDR_THUNDERX) {
        return 1;
    } else {
        return 0;
    }
}
#endif
