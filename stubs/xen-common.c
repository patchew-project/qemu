/*
 * Copyright (C) 2014       Citrix Systems UK Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/xen/xen.h"

void xenstore_store_pv_console_info(int i, Chardev *chr)
{
}

char *xen_blk_get_attached_dev_id(void *dev)
{
    return g_strdup("");
}

void xen_blk_resize_cb(void *dev)
{
}
