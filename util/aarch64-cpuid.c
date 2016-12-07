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
#include "qemu/cutils.h"
#include "qemu/aarch64-cpuid.h"

#if defined(__aarch64__)
static uint64_t qemu_read_aarch64_midr_el1(void)
{
    const char *file = "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1";
    char *buf;
    uint64_t midr = 0;

    if (!g_file_get_contents(file, &buf, 0, NULL)) {
        goto out;
    }

    if (qemu_strtoull(buf, NULL, 0, &midr) < 0) {
        midr = 0;
        goto out;
    }

out:
    g_free(buf);

    return midr;
}

static uint64_t aarch64_midr_val;
uint64_t get_aarch64_cpu_id(void)
{
    aarch64_midr_val = qemu_read_aarch64_midr_el1();
    aarch64_midr_val &= CPU_MODEL_MASK;

    return aarch64_midr_val;
}

bool is_thunderx_pass2_cpu(void)
{
    return aarch64_midr_val == MIDR_THUNDERX_PASS2;
}
#endif
