/*
 * 9p utilities (FreeBSD Implementation)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "qemu/xattr.h"
#include "9p-util.h"

static const char *mangle_xattr_name(const char *name)
{
    /*
     * ZFS forbits attributes in the user namespace starting with "user.".
     */
    if (strncmp(name, "user.", 5) == 0) {
        return name + 5;
    }
    return name;
}

ssize_t fgetxattr(int fd, const char *name, void *value, size_t size)
{
    name = mangle_xattr_name(name);
    return extattr_get_fd(fd, EXTATTR_NAMESPACE_USER, name, value, size);
}

ssize_t fgetxattrat_nofollow(int dirfd, const char *filename, const char *name,
                             void *value, size_t size)
{
    ssize_t ret;
    int fd;

    fd = openat_file(dirfd, filename,
                     O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    name = mangle_xattr_name(name);
    ret = extattr_get_fd(fd, EXTATTR_NAMESPACE_USER, name, value, size);
    close_preserve_errno(fd);
    return ret;
}

ssize_t flistxattrat_nofollow(int dirfd, const char *filename,
                              char *list, size_t size)
{
    ssize_t ret;
    int fd;

    fd = openat_file(dirfd, filename,
                     O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    ret = extattr_list_fd(fd, EXTATTR_NAMESPACE_USER, list, size);
    close_preserve_errno(fd);
    return ret;
}

ssize_t fremovexattrat_nofollow(int dirfd, const char *filename,
                                const char *name)
{
    int fd, ret;

    fd = openat_file(dirfd, filename,
                     O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    name = mangle_xattr_name(name);
    ret = extattr_delete_fd(fd, EXTATTR_NAMESPACE_USER, name);
    close_preserve_errno(fd);
    return ret;
}

int fsetxattrat_nofollow(int dirfd, const char *filename, const char *name,
                         void *value, size_t size, int flags)
{
    ssize_t ret;
    int fd;

    name = mangle_xattr_name(name);
    if (flags == (XATTR_CREATE | XATTR_REPLACE)) {
        errno = EINVAL;
        return -1;
    }
    fd = openat_file(dirfd, filename,
                     O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    if (flags & (XATTR_CREATE | XATTR_REPLACE)) {
        ret = extattr_get_fd(fd, EXTATTR_NAMESPACE_USER, name, NULL, 0);
        if (ret == -1 && errno != ENOATTR) {
            close_preserve_errno(fd);
            return -1;
        }
        if (ret >= 0 && (flags & XATTR_CREATE)) {
            errno = EEXIST;
            close_preserve_errno(fd);
            return -1;
        }
        if (ret == -1 && (flags & XATTR_REPLACE)) {
            errno = ENOATTR;
            close_preserve_errno(fd);
            return -1;
        }
    }
    ret = extattr_set_fd(fd, EXTATTR_NAMESPACE_USER, name, value, size);
    close_preserve_errno(fd);
    return ret;
}

int qemu_mknodat(int dirfd, const char *filename, mode_t mode, dev_t dev)
{
    return mknodat(dirfd, filename, mode, dev);
}
