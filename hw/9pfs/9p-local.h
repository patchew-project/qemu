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

#define VIRTFS_META_DIR ".virtfs_metadata"
#define VIRTFS_META_ROOT_FILE VIRTFS_META_DIR "_root"

#define ATTR_MAX 100

typedef struct {
    int mountfd;
} LocalData;

static inline bool local_is_mapped_file_metadata(FsContext *fs_ctx,
                                                 const char *name)
{
    return
        !strcmp(name, VIRTFS_META_DIR) || !strcmp(name, VIRTFS_META_ROOT_FILE);
}

int local_open_nofollow(FsContext *fs_ctx, const char *path, int flags,
                        mode_t mode);
#ifndef CONFIG_WIN32
int local_opendir_nofollow(FsContext *fs_ctx, const char *path);
#else
DIR *local_opendir_nofollow(FsContext *fs_ctx, const char *path);
#endif

#endif
