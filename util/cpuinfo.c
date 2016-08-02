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
#endif
