/*
 * fw_cfg stubs
 *
 * Copyright (c) 2019,2021 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/nvram/fw_cfg.h"

FWCfgState *fw_cfg_find(void)
{
    return NULL;
}

bool fw_cfg_add_from_generator(FWCfgState *s, const char *filename,
                               const char *gen_id, Error **errp)
{
    error_setg(errp, "fw-cfg device not built in");

    return true;
}

void fw_cfg_add_file(FWCfgState *s,  const char *filename,
                     void *data, size_t len)
{
    g_assert_not_reached();
}

void fw_cfg_add_file_callback(FWCfgState *s,  const char *filename,
                              FWCfgCallback select_cb,
                              FWCfgWriteCallback write_cb,
                              void *callback_opaque,
                              void *data, size_t len, bool read_only)
{
    g_assert_not_reached();
}

void *fw_cfg_modify_file(FWCfgState *s, const char *filename,
                        void *data, size_t len)
{
    g_assert_not_reached();
}

void fw_cfg_set_order_override(FWCfgState *s, int order)
{
    g_assert_not_reached();
}

void fw_cfg_reset_order_override(FWCfgState *s)
{
    g_assert_not_reached();
}

bool fw_cfg_dma_enabled(void *opaque)
{
    g_assert_not_reached();
}
