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

#include <math.h>
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qemu/aarch64-cpuid.h"

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

#define MIDR_THUNDERX_PASS2  \
               MIDR_CPU_PART(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX)
#define CPU_MODEL_MASK  (MIDR_IMPLEMENTER_MASK | MIDR_ARCHITECTURE_MASK | \
                         MIDR_PARTNUM_MASK)

static uint64_t qemu_read_aarch64_midr_el1(void)
{
#ifdef CONFIG_LINUX
    const char *file = "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1";
    char *buf;
    uint64_t midr = 0;

#define BUF_SIZE 32
    buf = g_malloc0(BUF_SIZE);
    if (!buf) {
        return 0;
    }

    if (!g_file_get_contents(file, &buf, 0, NULL)) {
        goto out;
    }

    if (qemu_strtoull(buf, NULL, 0, &midr) < 0) {
        goto out;
    }

out:
    g_free(buf);

    return midr;
#else
    return 0;
#endif
}

static uint64_t aarch64_midr_val;
uint64_t get_aarch64_cpu_id(void)
{
#ifdef CONFIG_LINUX
    aarch64_midr_val = qemu_read_aarch64_midr_el1();
    aarch64_midr_val &= CPU_MODEL_MASK;

    return aarch64_midr_val;
#else
    return 0;
#endif
}

bool is_thunderx_pass2_cpu(void)
{
    return aarch64_midr_val == MIDR_THUNDERX_PASS2;
}
#endif
