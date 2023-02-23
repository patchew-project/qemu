/*
 * Windows crashdump
 *
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef WIN_DUMP_H
#define WIN_DUMP_H

#include "sysemu/dump.h"

/* Windows dump is available only if target is x86_64 */
bool win_dump_available(Error **errp);

void create_win_dump(DumpState *s, Error **errp);

#endif /* WIN_DUMP_H */
