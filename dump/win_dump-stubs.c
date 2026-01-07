/*
 * Windows crashdump stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "win_dump.h"

bool win_dump_available(Error **errp)
{
    error_setg(errp, "Windows dump is only available on Windows");

    return false;
}

void create_win_dump(DumpState *s, Error **errp)
{
    g_assert_not_reached();
}
