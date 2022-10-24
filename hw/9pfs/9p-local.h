/*
 * 9p local backend utilities
 *
 * Copyright IBM, Corp. 2017
 *
 * Authors:
 *  Greg Kurz <groug@kaod.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_9P_LOCAL_H
#define QEMU_9P_LOCAL_H

#include "9p-file-id.h"

P9_FILE_ID local_open_nofollow(FsContext *fs_ctx, const char *path, int flags,
                               mode_t mode);
P9_FILE_ID local_opendir_nofollow(FsContext *fs_ctx, const char *path);

#endif
