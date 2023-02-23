/*
 * Windows crashdump stubs for non-x86 targets
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "win_dump.h"

bool win_dump_available(Error **errp)
{
    error_setg(errp, "Windows dump is only available for x86-64");

    return false;
}

void create_win_dump(DumpState *s, Error **errp)
{
    win_dump_available(errp);
}
