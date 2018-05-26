/*
 * 9p utilities
 *
 * Copyright IBM, Corp. 2017
 *
 * Authors:
 *  Greg Kurz <groug@kaod.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/xattr.h"
#include "9p-util.h"

ssize_t fgetxattrat_nofollow(int dirfd, const char *filename, const char *name,
                             void *value, size_t size)
{
    int ret;
#ifdef CONFIG_DARWIN
    int fd = openat_file(dirfd, filename, O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1)
        return -1;

    ret = fgetxattr(fd, name, value, size, 0, XATTR_NOFOLLOW);
    close_preserve_errno(fd);
#else
    char *proc_path = g_strdup_printf("/proc/self/fd/%d/%s", dirfd, filename);

    ret = lgetxattr(proc_path, name, value, size);
    g_free(proc_path);
#endif
    return ret;
}

ssize_t fgetxattr_follow(int fd, const char *name,
                         void *value, size_t size)
{
#ifdef CONFIG_DARWIN
    return fgetxattr(fd, name, value, size, 0, 0);
#else
    return fgetxattr(fd, name, value, size);
#endif
}

ssize_t flistxattrat_nofollow(int dirfd, const char *filename,
                              char *list, size_t size)
{
    int ret;
#ifdef CONFIG_DARWIN
    int fd = openat_file(dirfd, filename, O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1)
        return -1;

    ret = flistxattr(fd, list, size, XATTR_NOFOLLOW);
    close_preserve_errno(fd);
#else
    char *proc_path = g_strdup_printf("/proc/self/fd/%d/%s", dirfd, filename);

    ret = llistxattr(proc_path, list, size);
    g_free(proc_path);
#endif
    return ret;
}

ssize_t fremovexattrat_nofollow(int dirfd, const char *filename,
                                const char *name)
{
    int ret;
#ifdef CONFIG_DARWIN
    int fd = openat_file(dirfd, filename, O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1)
        return -1;

    ret = fremovexattr(fd, name, XATTR_NOFOLLOW);
    close_preserve_errno(fd);
    return ret;
#else
    char *proc_path = g_strdup_printf("/proc/self/fd/%d/%s", dirfd, filename);

    ret = lremovexattr(proc_path, name);
    g_free(proc_path);
    return ret;
#endif
}

int fsetxattrat_nofollow(int dirfd, const char *filename, const char *name,
                         void *value, size_t size, int flags)
{
    int ret;
#ifdef CONFIG_DARWIN
    int fd = openat_file(dirfd, filename, O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1)
        return -1;

    ret = fsetxattr(fd, name, value, size, 0, XATTR_NOFOLLOW);
    close_preserve_errno(fd);
#else
    char *proc_path = g_strdup_printf("/proc/self/fd/%d/%s", dirfd, filename);

    ret = lsetxattr(proc_path, name, value, size, flags);
    g_free(proc_path);
#endif
    return ret;
}
