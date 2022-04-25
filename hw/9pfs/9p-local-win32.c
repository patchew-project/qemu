/*
 * 9p Windows callback
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * Based on hw/9pfs/9p-local.c
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include <windows.h>
#include <dirent.h>
#include "9p.h"
#include "9p-local.h"
#include "9p-xattr.h"
#include "9p-util.h"
#include "fsdev/qemu-fsdev.h"   /* local_ops */
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include <libgen.h>

static inline int openfile_with_ctx(FsContext *fs_ctx, const char *name,
                                    int flags, mode_t mode)
{
    char *full_file_name;
    int fd;

    full_file_name = merge_fs_path(fs_ctx->fs_root, name);
    fd = open(full_file_name, flags | _O_BINARY, mode);
    g_free(full_file_name);

    return fd;
}

static inline DIR *opendir_with_ctx(FsContext *fs_ctx, const char *name)
{
    char *full_file_name;
    DIR *dir;

    full_file_name = merge_fs_path(fs_ctx->fs_root, name);
    dir = opendir(full_file_name);
    g_free(full_file_name);
    return dir;
}

int local_open_nofollow(FsContext *fs_ctx, const char *path, int flags,
                        mode_t mode)
{
    int fd;

    if (path[strlen(path) - 1] == '/' || (flags & O_DIRECTORY) != 0) {
        /* Windows does not allow call open() for a directory */
        fd = -1;
    } else {
        fd = openfile_with_ctx(fs_ctx, path, flags, mode);
    }

    return fd;
}

DIR *local_opendir_nofollow(FsContext *fs_ctx, const char *path)
{
    return opendir_with_ctx(fs_ctx, path);
}

static FILE *local_fopenat(const char *dirname, const char *name,
                           const char *mode)
{
    char *full_file_name;
    char modestr[3] = {0};
    FILE *fp;

    /*
     * only supports two modes
     */
    if (mode[0] == 'r') {
        modestr[0] = 'r';
    } else if (mode[0] == 'w') {
        modestr[0] = 'w';
    } else {
        return NULL;
    }
    /* Windows host needs 'b' flag */
    modestr[1] = 'b';

    full_file_name = merge_fs_path(dirname, name);
    fp = fopen(full_file_name, modestr);
    g_free(full_file_name);

    return fp;
}

static void local_mapped_file_attr(const char *dirpath, const char *name,
                                   struct stat *stbuf)
{
    FILE *fp;
    char buf[ATTR_MAX];
    char *full_file_name;

    if (strcmp(name, ".") != 0) {
        full_file_name = merge_fs_path(dirpath, VIRTFS_META_DIR);
        fp = local_fopenat(full_file_name, name, "r");
        g_free(full_file_name);
    } else {
        fp = local_fopenat(dirpath, VIRTFS_META_ROOT_FILE, "r");
    }
    if (!fp) {
        return;
    }

    memset(buf, 0, ATTR_MAX);
    while (fgets(buf, ATTR_MAX, fp)) {
        if (!strncmp(buf, "virtfs.uid", 10)) {
            stbuf->st_uid = atoi(buf + 11);
        } else if (!strncmp(buf, "virtfs.gid", 10)) {
            stbuf->st_gid = atoi(buf + 11);
        } else if (!strncmp(buf, "virtfs.mode", 11)) {
            stbuf->st_mode = (stbuf->st_mode & ~0777);
            stbuf->st_mode |= (atoi(buf + 12) & 0777);
        } else if (!strncmp(buf, "virtfs.rdev", 11)) {
            stbuf->st_rdev = atoi(buf + 12);
        }
        memset(buf, 0, ATTR_MAX);
    }
    fclose(fp);
}

static int local_lstat(FsContext *fs_ctx, V9fsPath *fs_path, struct stat *stbuf)
{
    int err = -1;
    char *full_dir_name, *full_file_name;
    char *dirpath = g_path_get_dirname(fs_path->data);
    char *name = g_path_get_basename(fs_path->data);

    full_dir_name = merge_fs_path(fs_ctx->fs_root, dirpath);
    full_file_name = merge_fs_path(full_dir_name, name);
    err = stat(full_file_name, stbuf);

    if (err == 0 && strcmp(fs_path->data, ".") == 0) {
        /*
         * Hard code for root directory on Windows host.
         * This will root directory have a special inode number,
         * then guest OS can detect it is a special directory.
         */
        stbuf->st_ino = 2;
    }

    if (fs_ctx->export_flags & V9FS_SM_MAPPED) {
        /* Actual credentials are part of extended attrs */
        uid_t tmp_uid;
        gid_t tmp_gid;
        mode_t tmp_mode;
        dev_t tmp_dev;

        if (fgetxattrat_nofollow(full_dir_name, name, "user.virtfs.uid",
                                 &tmp_uid, sizeof(uid_t)) > 0) {
            stbuf->st_uid = le32_to_cpu(tmp_uid);
        }
        if (fgetxattrat_nofollow(full_dir_name, name, "user.virtfs.gid",
                                 &tmp_gid, sizeof(gid_t)) > 0) {
            stbuf->st_gid = le32_to_cpu(tmp_gid);
        }
        if (fgetxattrat_nofollow(full_dir_name, name, "user.virtfs.mode",
                                 &tmp_mode, sizeof(mode_t)) > 0) {
            stbuf->st_mode = (stbuf->st_mode & ~0777);
            stbuf->st_mode |= le32_to_cpu(tmp_mode);
        }
        if (fgetxattrat_nofollow(full_dir_name, name, "user.virtfs.rdev",
                                 &tmp_dev, sizeof(dev_t)) > 0) {
            stbuf->st_rdev = le64_to_cpu(tmp_dev);
        }
    } else if (fs_ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        local_mapped_file_attr(full_dir_name, name, stbuf);
    }

    g_free(full_file_name);
    g_free(full_dir_name);

    if (err) {
        goto err_out;
    }

err_out:
    g_free(name);
    g_free(dirpath);
    return err;
}

static int local_set_mapped_file_attrat(const char *dirname, const char *name,
                                        FsCred *credp)
{
    FILE *fp;
    int ret;
    char buf[ATTR_MAX];
    int uid = -1, gid = -1, mode = -1, rdev = -1;
    bool is_root = !strcmp(name, ".");
    char *full_dir_name;

    if (is_root) {
        fp = local_fopenat(dirname, VIRTFS_META_ROOT_FILE, "r");
        if (!fp) {
            if (errno == ENOENT) {
                goto update_map_file;
            } else {
                return -1;
            }
        }
    } else {
        /*
         * mapped-file:
         *   <sub_file> attribute stored to:
         *   <directory> + VIRTFS_META_DIR + <sub_file_name>
         */
        full_dir_name = merge_fs_path(dirname, VIRTFS_META_DIR);
        ret = mkdir(full_dir_name);

        if (ret < 0 && errno != EEXIST) {
            g_free(full_dir_name);
            return -1;
        }

        fp = local_fopenat(full_dir_name, name, "r");
        if (!fp) {
            if (errno == ENOENT) {
                goto update_map_file;
            } else {
                g_free(full_dir_name);
                return -1;
            }
        }
    }

    memset(buf, 0, ATTR_MAX);
    while (fgets(buf, ATTR_MAX, fp)) {
        if (!strncmp(buf, "virtfs.uid", 10)) {
            uid = atoi(buf + 11);
        } else if (!strncmp(buf, "virtfs.gid", 10)) {
            gid = atoi(buf + 11);
        } else if (!strncmp(buf, "virtfs.mode", 11)) {
            mode = atoi(buf + 12);
        } else if (!strncmp(buf, "virtfs.rdev", 11)) {
            rdev = atoi(buf + 12);
        }
        memset(buf, 0, ATTR_MAX);
    }
    fclose(fp);

update_map_file:
    if (is_root) {
        fp = local_fopenat(dirname, VIRTFS_META_ROOT_FILE, "w");
    } else {
        fp = local_fopenat(full_dir_name, name, "w");
        g_free(full_dir_name);
    }
    if (!fp) {
        return -1;
    }

    if (credp->fc_uid != -1) {
        uid = credp->fc_uid;
    }
    if (credp->fc_gid != -1) {
        gid = credp->fc_gid;
    }
    if (credp->fc_mode != (mode_t)-1) {
        mode = credp->fc_mode;
    }
    if (credp->fc_rdev != -1) {
        rdev = credp->fc_rdev;
    }

    if (uid != -1) {
        fprintf(fp, "virtfs.uid=%d\n", uid);
    }
    if (gid != -1) {
        fprintf(fp, "virtfs.gid=%d\n", gid);
    }
    if (mode != -1) {
        fprintf(fp, "virtfs.mode=%d\n", mode);
    }
    if (rdev != -1) {
        fprintf(fp, "virtfs.rdev=%d\n", rdev);
    }
    fclose(fp);

    return 0;
}

static int local_set_xattrat(const char *dirname, const char *path,
                             FsCred *credp)
{
    int err;

    if (credp->fc_uid != -1) {
        uint32_t tmp_uid = cpu_to_le32(credp->fc_uid);
        err = fsetxattrat_nofollow(dirname, path, "user.virtfs.uid",
                                   &tmp_uid, sizeof(uid_t), 0);
        if (err) {
            return err;
        }
    }
    if (credp->fc_gid != -1) {
        uint32_t tmp_gid = cpu_to_le32(credp->fc_gid);
        err = fsetxattrat_nofollow(dirname, path, "user.virtfs.gid",
                                   &tmp_gid, sizeof(gid_t), 0);
        if (err) {
            return err;
        }
    }
    if (credp->fc_mode != (mode_t)-1) {
        uint32_t tmp_mode = cpu_to_le32(credp->fc_mode);
        err = fsetxattrat_nofollow(dirname, path, "user.virtfs.mode",
                                   &tmp_mode, sizeof(mode_t), 0);
        if (err) {
            return err;
        }
    }
    if (credp->fc_rdev != -1) {
        uint64_t tmp_rdev = cpu_to_le64(credp->fc_rdev);
        err = fsetxattrat_nofollow(dirname, path, "user.virtfs.rdev",
                                   &tmp_rdev, sizeof(dev_t), 0);
        if (err) {
            return err;
        }
    }
    return 0;
}

static ssize_t local_readlink(FsContext *fs_ctx, V9fsPath *fs_path,
                              char *buf, size_t bufsz)
{
    return -1;
}

static int local_close(FsContext *ctx, V9fsFidOpenState *fs)
{
    return close(fs->fd);
}

static int local_closedir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return closedir(fs->dir.stream);
}

static int local_open(FsContext *ctx, V9fsPath *fs_path,
                      int flags, V9fsFidOpenState *fs)
{
    int fd;

    fd = local_open_nofollow(ctx, fs_path->data, flags, 0);
    if (fd == -1) {
        return -1;
    }
    fs->fd = fd;
    return fs->fd;
}

static int local_opendir(FsContext *ctx,
                         V9fsPath *fs_path, V9fsFidOpenState *fs)
{
    DIR *stream;
    char *full_file_name;

    full_file_name = merge_fs_path(ctx->fs_root, fs_path->data);
    stream = opendir(full_file_name);
    g_free(full_file_name);

    if (!stream) {
        return -1;
    }

    fs->dir.stream = stream;
    return 0;
}

static void local_rewinddir(FsContext *ctx, V9fsFidOpenState *fs)
{
    rewinddir(fs->dir.stream);
}

static off_t local_telldir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return telldir(fs->dir.stream);
}

static struct dirent *local_readdir(FsContext *ctx, V9fsFidOpenState *fs)
{
    struct dirent *entry;

again:
    entry = readdir(fs->dir.stream);
    if (!entry) {
        return NULL;
    }

    if (ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        if (local_is_mapped_file_metadata(ctx, entry->d_name)) {
            /* skip the meta data */
            goto again;
        }
    }

    return entry;
}

static void local_seekdir(FsContext *ctx, V9fsFidOpenState *fs, off_t off)
{
    off_t count;
    struct dirent *findentry;
    struct dirent *entry;
    size_t namelen[3] = {0};
    off_t direntoff[3] = {-1, -1, -1};
    char *d_name[3];
    int i;

    /*
     * MinGW's seekdir() requires directory is not modified. If guest OS is
     * modifying the directory when calling seekdir(), e.g.: "rm -rf *",
     * then MinGW's seekdir() will seek to a wrong offset.
     *
     * This function saves some old offset directory entry name,
     * and lookup current entry again, and compare the offset.
     *
     * If new offset is less than old offset, that means someone is deleting
     * files in the directory, thus we need to seek offset backward.
     *
     * If new offset is larger than old offset, that means someone is creating
     * files in the directory, thus we need to seek offset forward.
     */

    direntoff[0] = telldir(fs->dir.stream);

    /* do nothing if current offset is 0 or EOF */
    if (direntoff[0] == 0 || direntoff[0] < 0) {
        seekdir(fs->dir.stream, off);
        return ;
    }

    d_name[0] = g_malloc0(sizeof(entry->d_name) * 3);
    d_name[1] = d_name[0] + sizeof(entry->d_name);
    d_name[2] = d_name[1] + sizeof(entry->d_name);

    /* save 3 nearest entries and offsets */
    for (i = 0; i < 3; i++) {
        entry = &fs->dir.stream->dd_dir;

        memcpy(d_name[i], entry->d_name, entry->d_namlen);
        namelen[i] = strlen(d_name[i]) + 1;

        direntoff[i] = telldir(fs->dir.stream);

        entry = readdir(fs->dir.stream);
        if (entry == NULL) {
            break;
        }
    }

    /* lookup saved entries again */
    for (i = 0; i < 3 && direntoff[i] != -1; i++) {
        rewinddir(fs->dir.stream);
        count = 0;
        while ((findentry = readdir(fs->dir.stream)) != NULL) {
            count++;

            if (memcmp(findentry->d_name, d_name[i], namelen[i]) == 0) {
                if (count + i == direntoff[i]) {
                    seekdir(fs->dir.stream, off);
                    goto out;
                } else if (count + i < direntoff[i]) {
                    off = off - (direntoff[i] - count) - i;
                    if (off <= 0) {
                        off = 0;
                    }
                    seekdir(fs->dir.stream, off);
                    goto out;
                } else {
                    off = off + (count - direntoff[i]) - i;
                    seekdir(fs->dir.stream, off);
                    goto out;
                }
            }
        }
    }
    /* can not get anything, seek backward */
    off = off - 1;

    seekdir(fs->dir.stream, off);
out:
    g_free(d_name[0]);
    return ;
}

static ssize_t local_preadv(FsContext *ctx, V9fsFidOpenState *fs,
                            const struct iovec *iov,
                            int iovcnt, off_t offset)
{
#ifdef CONFIG_PREADV
    return preadv(fs->fd, iov, iovcnt, offset);
#else
    int err = lseek(fs->fd, offset, SEEK_SET);
    if (err == -1) {
        return err;
    } else {
        return readv(fs->fd, iov, iovcnt);
    }
#endif
}

static ssize_t local_pwritev(FsContext *ctx, V9fsFidOpenState *fs,
                             const struct iovec *iov,
                             int iovcnt, off_t offset)
{
    ssize_t ret;
#ifdef CONFIG_PREADV
    ret = pwritev(fs->fd, iov, iovcnt, offset);
#else
    int err = lseek(fs->fd, offset, SEEK_SET);
    if (err == -1) {
        return err;
    } else {
        ret = writev(fs->fd, iov, iovcnt);
    }
#endif
#ifdef CONFIG_SYNC_FILE_RANGE
    if (ret > 0 && ctx->export_flags & V9FS_IMMEDIATE_WRITEOUT) {
        /*
         * Initiate a writeback. This is not a data integrity sync.
         * We want to ensure that we don't leave dirty pages in the cache
         * after write when writeout=immediate is sepcified.
         */
        sync_file_range(fs->fd, offset, ret,
                        SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE);
    }
#endif
    return ret;
}

static int local_chmod(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    char *dirpath = g_path_get_dirname(fs_path->data);
    char *name = g_path_get_basename(fs_path->data);
    int ret = -1;
    char *full_file_name = NULL;
    DIR *dir;
    dir = local_opendir_nofollow(fs_ctx, dirpath);
    if (dir == NULL) {
        goto out;
    }
    full_file_name = merge_fs_path(fs_ctx->fs_root, dirpath);

    if (fs_ctx->export_flags & V9FS_SM_MAPPED) {
        ret = local_set_xattrat(full_file_name, name, credp);
    } else if (fs_ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        ret = local_set_mapped_file_attrat(full_file_name, name, credp);
    } else if (fs_ctx->export_flags & V9FS_SM_PASSTHROUGH ||
               fs_ctx->export_flags & V9FS_SM_NONE) {
        ret = -1;
        errno = ENOTSUP;
    }
    closedir(dir);

out:
    if (full_file_name != NULL) {
        g_free(full_file_name);
    }

    g_free(dirpath);
    g_free(name);
    return ret;
}

static int local_mknod(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    return -1;
}

static int local_mkdir(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    int err = -1;
    char *full_file_name;
    char *full_dir_name;
    DIR *dir;

    if (fs_ctx->export_flags & V9FS_SM_MAPPED_FILE &&
        local_is_mapped_file_metadata(fs_ctx, name)) {
        errno = EINVAL;
        return -1;
    }

    dir = local_opendir_nofollow(fs_ctx, dir_path->data);
    if (dir == NULL) {
        return -1;
    }
    closedir(dir);

    full_dir_name = merge_fs_path(fs_ctx->fs_root, dir_path->data);
    full_file_name = merge_fs_path(full_dir_name, name);

    if (fs_ctx->export_flags & V9FS_SM_MAPPED ||
        fs_ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        err = mkdir(full_file_name);
        if (err == -1) {
            goto out;
        }
        credp->fc_mode = credp->fc_mode;

        if (fs_ctx->export_flags & V9FS_SM_MAPPED) {
            err = local_set_xattrat(full_dir_name, name, credp);
        } else {
            err = local_set_mapped_file_attrat(full_dir_name, name, credp);
        }
        if (err == -1) {
            rmdir(full_file_name);
        }
    } else if (fs_ctx->export_flags & V9FS_SM_PASSTHROUGH ||
               fs_ctx->export_flags & V9FS_SM_NONE) {
        err = mkdir(full_file_name);
        if (err == -1) {
            goto out;
        }
        /* Windows does not support chmod, do nothing here */
    }

    goto out;

out:
    g_free(full_dir_name);
    g_free(full_file_name);
    return err;
}

static int local_fstat(FsContext *fs_ctx, int fid_type,
                       V9fsFidOpenState *fs, struct stat *stbuf)
{

    int err, fd;
    char filename[NAME_MAX];
    char *dirpath;
    char *name;
    HANDLE hFile;
    DWORD dwRet;

    if (fid_type == P9_FID_DIR) {
        /* Windows does not support open directory */
        return -1;
    } else {
        fd = fs->fd;
    }

    err = fstat(fd, stbuf);
    if (err) {
        return err;
    }

    /* get real file name by fd */
    hFile = (HANDLE)_get_osfhandle(fd);
    dwRet = GetFinalPathNameByHandle(hFile, filename, sizeof(filename), 0);

    if (dwRet >= NAME_MAX) {
        return -1;
    }
    /* skip string "\\\\?\\" return from GetFinalPathNameByHandle() */
    memmove(filename, filename + 4, NAME_MAX - 4);

    dirpath = g_path_get_dirname(filename);
    name = g_path_get_basename(filename);

    if (fs_ctx->export_flags & V9FS_SM_MAPPED) {
        /* Actual credentials are part of extended attrs */
        uid_t tmp_uid;
        gid_t tmp_gid;
        mode_t tmp_mode;
        dev_t tmp_dev;

        if (fgetxattrat_nofollow(dirpath, name, "user.virtfs.uid",
                                 &tmp_uid, sizeof(uid_t)) > 0) {
            stbuf->st_uid = le32_to_cpu(tmp_uid);
        }
        if (fgetxattrat_nofollow(dirpath, name, "user.virtfs.gid",
                                 &tmp_gid, sizeof(gid_t)) > 0) {
            stbuf->st_gid = le32_to_cpu(tmp_gid);
        }
        if (fgetxattrat_nofollow(dirpath, name, "user.virtfs.mode",
                                 &tmp_mode, sizeof(mode_t)) > 0) {
            stbuf->st_mode = (stbuf->st_mode & ~0777);
            stbuf->st_mode |= le32_to_cpu(tmp_mode);
        }
        if (fgetxattrat_nofollow(dirpath, name, "user.virtfs.rdev",
                                 &tmp_dev, sizeof(dev_t)) > 0) {
            stbuf->st_rdev = le64_to_cpu(tmp_dev);
        }
    } else if (fs_ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        errno = EOPNOTSUPP;
        g_free(dirpath);
        g_free(name);
        return -1;
    }

    g_free(dirpath);
    g_free(name);

    return err;
}

static int local_open2(FsContext *fs_ctx, V9fsPath *dir_path, const char *name,
                       int flags, FsCred *credp, V9fsFidOpenState *fs)
{
    int fd = -1;
    int err = -1;
    char *full_file_name;

    if (fs_ctx->export_flags & V9FS_SM_MAPPED_FILE &&
        local_is_mapped_file_metadata(fs_ctx, name)) {
        errno = EINVAL;
        return -1;
    }

    full_file_name = merge_fs_path(dir_path->data, name);
    fd = openfile_with_ctx(fs_ctx, full_file_name, flags, credp->fc_mode);
    g_free(full_file_name);

    err = fd;
    fs->fd = fd;
    goto out;

    close_preserve_errno(fd);
out:
    return err;
}


static int local_symlink(FsContext *fs_ctx, const char *oldpath,
                         V9fsPath *dir_path, const char *name, FsCred *credp)
{
    return -1;
}

static int local_link(FsContext *ctx, V9fsPath *oldpath,
                      V9fsPath *dirpath, const char *name)
{
    return -1;
}

static int local_truncate(FsContext *ctx, V9fsPath *fs_path, off_t size)
{
    int fd, ret;

    fd = local_open_nofollow(ctx, fs_path->data, O_WRONLY, 0);
    if (fd == -1) {
        return -1;
    }
    ret = ftruncate(fd, size);
    close_preserve_errno(fd);
    return ret;
}

static int local_chown(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    char *full_file_name;
    char *dirpath = g_path_get_dirname(fs_path->data);
    char *name = g_path_get_basename(fs_path->data);
    int ret = -1;
    DIR *dir;

    dir = local_opendir_nofollow(fs_ctx, dirpath);
    if (dir == NULL) {
        goto out;
    }
    full_file_name = merge_fs_path(fs_ctx->fs_root, dirpath);

    if ((credp->fc_uid == -1 && credp->fc_gid == -1) ||
        (fs_ctx->export_flags & V9FS_SM_PASSTHROUGH) ||
        (fs_ctx->export_flags & V9FS_SM_NONE)) {
        /* Windows does not support chown() */
        ret = -1;
        errno = ENOTSUP;
    } else if (fs_ctx->export_flags & V9FS_SM_MAPPED) {
        ret = local_set_xattrat(full_file_name, name, credp);
    } else if (fs_ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        ret = local_set_mapped_file_attrat(full_file_name, name, credp);
    }
    g_free(full_file_name);
    closedir(dir);
out:
    g_free(name);
    g_free(dirpath);
    return ret;
}

static int local_utimensat(FsContext *s, V9fsPath *fs_path,
                           const struct timespec *buf)
{
    struct utimbuf tm;
    char *full_file_name;
    int err;

    tm.actime = buf[0].tv_sec;
    tm.modtime = buf[1].tv_sec;

    full_file_name = merge_fs_path(s->fs_root, fs_path->data);
    err = utime(full_file_name, &tm);
    g_free(full_file_name);

    return err;
}

static int local_unlinkat_common(FsContext *ctx, const char *dirname,
                                 const char *name, int flags)
{
    int ret;
    char *full_file_name;
    char *full_dir_name;

    full_dir_name = merge_fs_path(ctx->fs_root, dirname);
    full_file_name = merge_fs_path(full_dir_name, name);

    if (ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        char *full_meta_dir_name;
        char *full_meta_file_name;

        /*
         * We need to remove the metadata as well:
         * - the metadata directory if we're removing a directory
         * - the metadata file in the parent's metadata directory
         *
         * If any of these are missing (ie, ENOENT) then we're probably
         * trying to remove something that wasn't created in mapped-file
         * mode. We just ignore the error.
         */

        if ((flags & AT_REMOVEDIR) != 0) {
            full_meta_dir_name = merge_fs_path(full_file_name, VIRTFS_META_DIR);
            ret = rmdir(full_meta_dir_name);
            g_free(full_meta_dir_name);

            if (ret < 0 && errno != ENOENT) {
                g_free(full_file_name);
                g_free(full_dir_name);
                return -1;
            }
        }

        full_meta_dir_name = merge_fs_path(full_dir_name, VIRTFS_META_DIR);
        full_meta_file_name = merge_fs_path(full_meta_dir_name, name);
        ret = remove(full_meta_file_name);
        g_free(full_meta_dir_name);
        g_free(full_meta_file_name);

        if (ret < 0 && errno != ENOENT) {
            g_free(full_dir_name);
            g_free(full_file_name);

            return -1;
        }
    }

    if ((flags & AT_REMOVEDIR) != 0) {
        ret = rmdir(full_file_name);
    } else {
        ret = remove(full_file_name);
    }

    g_free(full_dir_name);
    g_free(full_file_name);

    return ret;
}

static int local_remove(FsContext *ctx, const char *path)
{
    int err;
    DIR *stream;
    char *full_file_name;
    char *dirpath = g_path_get_dirname(path);
    char *name = g_path_get_basename(path);
    int flags = 0;

    full_file_name = merge_fs_path(ctx->fs_root, path);
    stream = opendir(full_file_name);
    if (stream != NULL) {
        closedir(stream);
        flags |= AT_REMOVEDIR;
    }
    err = local_unlinkat_common(ctx, dirpath, name, flags);

    g_free(name);
    g_free(dirpath);
    g_free(full_file_name);
    return err;
}

static int local_fsync(FsContext *ctx, int fid_type,
                       V9fsFidOpenState *fs, int datasync)
{
    if (fid_type != P9_FID_DIR) {
        return _commit(fs->fd);
    }
    return 0;
}

static int local_statfs(FsContext *s, V9fsPath *fs_path, struct statfs *stbuf)
{
    int ret;
    ret = qemu_statfs(s->fs_root, stbuf);
    if (ret == 0) {
        /* use context address as fsid */
        memcpy(&stbuf->f_fsid, s, sizeof(long));
    }

    return ret;
}

static ssize_t local_lgetxattr(FsContext *ctx, V9fsPath *fs_path,
                               const char *name, void *value, size_t size)
{
    return -1;
}

static ssize_t local_llistxattr(FsContext *ctx, V9fsPath *fs_path,
                                void *value, size_t size)
{
    return -1;
}

static int local_lsetxattr(FsContext *ctx, V9fsPath *fs_path, const char *name,
                           void *value, size_t size, int flags)
{
    return -1;
}

static int local_lremovexattr(FsContext *ctx, V9fsPath *fs_path,
                              const char *name)
{
    return -1;
}

static int local_name_to_path(FsContext *ctx, V9fsPath *dir_path,
                              const char *name, V9fsPath *target)
{
    if (ctx->export_flags & V9FS_SM_MAPPED_FILE &&
        local_is_mapped_file_metadata(ctx, name)) {
        errno = EINVAL;
        return -1;
    }

    if (dir_path) {
        if (!strcmp(name, ".")) {
            /* "." relative to "foo/bar" is "foo/bar" */
            v9fs_path_copy(target, dir_path);
        } else if (!strcmp(name, "..")) {
            if (!strcmp(dir_path->data, ".")) {
                /* ".." relative to the root is "." */
                v9fs_path_sprintf(target, ".");
            } else {
                char *tmp = g_path_get_dirname(dir_path->data);
                /*
                 * Symbolic links are resolved by the client. We can assume
                 * that ".." relative to "foo/bar" is equivalent to "foo"
                 */
                v9fs_path_sprintf(target, "%s", tmp);
                g_free(tmp);
            }
        } else {
            assert(!strchr(name, '/'));
            v9fs_path_sprintf(target, "%s/%s", dir_path->data, name);
        }
    } else if (!strcmp(name, "/") || !strcmp(name, ".") ||
               !strcmp(name, "..")) {
            /* This is the root fid */
        v9fs_path_sprintf(target, ".");
    } else {
        assert(!strchr(name, '/'));
        v9fs_path_sprintf(target, "./%s", name);
    }
    return 0;
}

static int local_renameat(FsContext *ctx, V9fsPath *olddir,
                          const char *old_name, V9fsPath *newdir,
                          const char *new_name)
{
    return -1;
}

static int local_rename(FsContext *ctx, const char *oldpath,
                        const char *newpath)
{
    int err;

    char *full_old_name;
    char *full_new_name;

    full_old_name = merge_fs_path(ctx->fs_root, oldpath);
    full_new_name = merge_fs_path(ctx->fs_root, newpath);

    err = rename(full_old_name, full_new_name);

    g_free(full_old_name);
    g_free(full_new_name);

    return err;
}

static int local_unlinkat(FsContext *ctx, V9fsPath *dir,
                          const char *name, int flags)
{
    int ret;

    if (ctx->export_flags & V9FS_SM_MAPPED_FILE &&
        local_is_mapped_file_metadata(ctx, name)) {
        errno = EINVAL;
        return -1;
    }

    ret = local_unlinkat_common(ctx, dir->data, name, flags);

    return ret;
}

static int check_filesystem_type(char *fs_root, int export_flags)
{
    HANDLE hFile;
    wchar_t FsName[MAX_PATH + 1] = {0};
    wchar_t NtfsName[5] = {'N', 'T', 'F', 'S'};

    if ((export_flags & V9FS_SM_MAPPED) == 0) {
        return 0;
    }

    hFile = CreateFile(fs_root, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    /* Get file system type name */
    if (GetVolumeInformationByHandleW(hFile, NULL, 0, NULL, NULL, NULL,
                                      FsName, MAX_PATH + 1) == 0) {
        CloseHandle(hFile);
        return -1;
    }
    CloseHandle(hFile);

    if (wcscmp(FsName, NtfsName) != 0) {
        return -1;
    }

    return 0;
}

static int local_init(FsContext *ctx, Error **errp)
{
    LocalData *data = g_malloc(sizeof(*data));

    struct stat StatBuf;

    if (stat(ctx->fs_root, &StatBuf) != 0) {
        error_setg_errno(errp, errno, "failed to open '%s'", ctx->fs_root);
        goto err;
    }

    /*
     * security_model=mapped(-xattr) requires a fileystem on Windows that
     * supports Alternate Data Stream (ADS). NTFS is one of them, and is
     * probably most popular on Windows. It is fair enough to assume
     * Windows users to use NTFS for the mapped security model.
     */
    if (check_filesystem_type(ctx->fs_root, ctx->export_flags) != 0) {
        error_setg_errno(errp, EINVAL, "require NTFS file system when "
                         "security_model is mapped or mapped-xattr");
        goto err;
    }

    if (ctx->export_flags & V9FS_SM_PASSTHROUGH) {
        ctx->xops = passthrough_xattr_ops;
    } else if (ctx->export_flags & V9FS_SM_MAPPED) {
        ctx->xops = mapped_xattr_ops;
    } else if (ctx->export_flags & V9FS_SM_NONE) {
        ctx->xops = none_xattr_ops;
    } else if (ctx->export_flags & V9FS_SM_MAPPED_FILE) {
        /*
         * xattr operation for mapped-file and passthrough
         * remain same.
         */
        ctx->xops = passthrough_xattr_ops;
    }
    ctx->export_flags |= V9FS_PATHNAME_FSCONTEXT;

    ctx->private = data;
    return 0;

err:
    g_free(data);
    return -1;
}

static void local_cleanup(FsContext *ctx)
{
    LocalData *data = ctx->private;

    if (!data) {
        return;
    }

    close(data->mountfd);
    g_free(data);
}

static void error_append_security_model_hint(Error *const *errp)
{
    error_append_hint(errp, "Valid options are: security_model="
                      "[passthrough|mapped-xattr|mapped-file|none]\n");
}

static int local_parse_opts(QemuOpts *opts, FsDriverEntry *fse, Error **errp)
{
    ERRP_GUARD();
    const char *sec_model = qemu_opt_get(opts, "security_model");
    const char *path = qemu_opt_get(opts, "path");
    const char *multidevs = qemu_opt_get(opts, "multidevs");

    if (!sec_model) {
        error_setg(errp, "security_model property not set");
        error_append_security_model_hint(errp);
        return -1;
    }

    if (!strcmp(sec_model, "passthrough")) {
        fse->export_flags |= V9FS_SM_PASSTHROUGH;
    } else if (!strcmp(sec_model, "mapped") ||
               !strcmp(sec_model, "mapped-xattr")) {
        fse->export_flags |= V9FS_SM_MAPPED;
    } else if (!strcmp(sec_model, "none")) {
        fse->export_flags |= V9FS_SM_NONE;
    } else if (!strcmp(sec_model, "mapped-file")) {
        fse->export_flags |= V9FS_SM_MAPPED_FILE;
    } else {
        error_setg(errp, "invalid security_model property '%s'", sec_model);
        error_append_security_model_hint(errp);
        return -1;
    }

    if (multidevs) {
        if (!strcmp(multidevs, "remap")) {
            fse->export_flags &= ~V9FS_FORBID_MULTIDEVS;
            fse->export_flags |= V9FS_REMAP_INODES;
        } else if (!strcmp(multidevs, "forbid")) {
            fse->export_flags &= ~V9FS_REMAP_INODES;
            fse->export_flags |= V9FS_FORBID_MULTIDEVS;
        } else if (!strcmp(multidevs, "warn")) {
            fse->export_flags &= ~V9FS_FORBID_MULTIDEVS;
            fse->export_flags &= ~V9FS_REMAP_INODES;
        } else {
            error_setg(errp, "invalid multidevs property '%s'",
                       multidevs);
            error_append_hint(errp, "Valid options are: multidevs="
                              "[remap|forbid|warn]\n");
            return -1;
        }
    }

    if (!path) {
        error_setg(errp, "path property not set");
        return -1;
    }

    if (fsdev_throttle_parse_opts(opts, &fse->fst, errp)) {
        error_prepend(errp, "invalid throttle configuration: ");
        return -1;
    }

    if (fse->export_flags & V9FS_SM_MAPPED ||
        fse->export_flags & V9FS_SM_MAPPED_FILE) {
        fse->fmode =
            qemu_opt_get_number(opts, "fmode", SM_LOCAL_MODE_BITS) & 0777;
        fse->dmode =
            qemu_opt_get_number(opts, "dmode", SM_LOCAL_DIR_MODE_BITS) & 0777;
    } else {
        if (qemu_opt_find(opts, "fmode")) {
            error_setg(errp, "fmode is only valid for mapped security modes");
            return -1;
        }
        if (qemu_opt_find(opts, "dmode")) {
            error_setg(errp, "dmode is only valid for mapped security modes");
            return -1;
        }
    }

    fse->path = g_strdup(path);

    return 0;
}

FileOperations local_ops = {
    .parse_opts = local_parse_opts,
    .init  = local_init,
    .cleanup = local_cleanup,
    .lstat = local_lstat,
    .readlink = local_readlink,
    .close = local_close,
    .closedir = local_closedir,
    .open = local_open,
    .opendir = local_opendir,
    .rewinddir = local_rewinddir,
    .telldir = local_telldir,
    .readdir = local_readdir,
    .seekdir = local_seekdir,
    .preadv = local_preadv,
    .pwritev = local_pwritev,
    .chmod = local_chmod,
    .mknod = local_mknod,
    .mkdir = local_mkdir,
    .fstat = local_fstat,
    .open2 = local_open2,
    .symlink = local_symlink,
    .link = local_link,
    .truncate = local_truncate,
    .rename = local_rename,
    .chown = local_chown,
    .utimensat = local_utimensat,
    .remove = local_remove,
    .fsync = local_fsync,
    .statfs = local_statfs,
    .lgetxattr = local_lgetxattr,
    .llistxattr = local_llistxattr,
    .lsetxattr = local_lsetxattr,
    .lremovexattr = local_lremovexattr,
    .name_to_path = local_name_to_path,
    .renameat  = local_renameat,
    .unlinkat = local_unlinkat,
};
