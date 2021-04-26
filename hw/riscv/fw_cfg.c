/*
 * QEMU fw_cfg helpers (RISC-V specific)
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/mips/fw_cfg.h"
#include "hw/nvram/fw_cfg.h"

const char *fw_cfg_arch_key_name(uint16_t key)
{
    return NULL;
}
