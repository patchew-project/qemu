/*
 * QEMU monitor, target-dependent part
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "monitor-internal.h"
#include "monitor/qdev.h"
#include "net/slirp.h"
#include "system/device_tree.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#include "block/block-hmp-cmds.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-machine.h"

#if defined(TARGET_S390X)
#include "hw/s390x/storage-keys.h"
#include "hw/s390x/storage-attributes.h"
#endif

/* Make devices configuration available for use in hmp-commands*.hx templates */
#include CONFIG_DEVICES

/* Please update hmp-commands.hx when adding or changing commands */
static HMPCommand hmp_info_cmds[] = {
#include "hmp-commands-info.h"
    { NULL, NULL, },
};

/* hmp_cmds and hmp_info_cmds would be sorted at runtime */
static HMPCommand hmp_cmds[] = {
#include "hmp-commands.h"
    { NULL, NULL, },
};

HMPCommand *hmp_cmds_for_target(bool info_command)
{
    return info_command ? hmp_info_cmds : hmp_cmds;
}

static int
compare_mon_cmd(const void *a, const void *b)
{
    return strcmp(((const HMPCommand *)a)->name,
            ((const HMPCommand *)b)->name);
}

static void __attribute__((__constructor__)) sortcmdlist(void)
{
    qsort(hmp_cmds, ARRAY_SIZE(hmp_cmds) - 1,
          sizeof(*hmp_cmds),
          compare_mon_cmd);
    qsort(hmp_info_cmds, ARRAY_SIZE(hmp_info_cmds) - 1,
          sizeof(*hmp_info_cmds),
          compare_mon_cmd);
}
