/*
 * 9p utilities (Windows Implementation)
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * This file contains Windows only functions for 9pfs.
 *
 * For 9pfs Windows host, the following features are different from Linux host:
 *
 * 1. Windows POSIX API does not provide the NO_FOLLOW flag, that means MinGW
 *    cannot detect if a path is a symbolic link or not. Also Windows do not
 *    provide POSIX compatible readlink(). Supporting symbolic link in 9pfs on
 *    Windows may cause security issues, so symbolic link support is disabled
 *    completely for security model "none" or "passthrough".
 *
 * 2. Windows file system does not support extended attributes directly. 9pfs
 *    for Windows uses NTFS ADS (Alternate Data Streams) to emulate extended
 *    attributes.
 *
 * 3. statfs() is not available on Windows. qemu_statfs() is used to emulate it.
 *
 * 4. On Windows trying to open a directory with the open() API will fail.
 *    This is because Windows does not allow opening directory in normal usage.
 *
 *    As a result of this, all xxx_at() functions won't work directly on
 *    Windows, e.g.: openat(), unlinkat(), etc.
 *
 *    As xxx_at() can prevent parent directory to be modified on Linux host,
 *    to support this and prevent security issue, all xxx_at() APIs are replaced
 *    by xxx_at_win32().
 *
 *    Windows does not support opendir, the directory fd is created by
 *    CreateFile and convert to fd by _open_osfhandle(). Keep the fd open will
 *    lock and protect the directory (can not be modified or replaced)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "9p.h"
#include "9p-util.h"
#include "9p-local.h"

#include <windows.h>
#include <dirent.h>

#define V9FS_MAGIC  0x53465039  /* string "9PFS" */

/*
 * build_ads_name - construct Windows ADS name
 *
 * This function constructs Windows NTFS ADS (Alternate Data Streams) name
 * to <namebuf>.
 */
static int build_ads_name(char *namebuf, size_t namebuf_len,
                          const char *filename, const char *ads_name)
{
    size_t total_size;

    total_size = strlen(filename) + strlen(ads_name) + 2;
    if (total_size  > namebuf_len) {
        return -1;
    }

    /*
     * NTFS ADS (Alternate Data Streams) name format: filename:ads_name
     * e.g.: D:\1.txt:my_ads_name
     */

    strcpy(namebuf, filename);
    strcat(namebuf, ":");
    strcat(namebuf, ads_name);

    return 0;
}

/*
 * copy_ads_name - copy ADS name from buffer returned by FindNextStreamW()
 *
 * This function removes string "$DATA" in ADS name string returned by
 * FindNextStreamW(), and copies the real ADS name to <namebuf>.
 */
static ssize_t copy_ads_name(char *namebuf, size_t namebuf_len,
                             char *full_ads_name)
{
    char *p1, *p2;

    /*
     * NTFS ADS (Alternate Data Streams) name from enumerate data format:
     * :ads_name:$DATA, e.g.: :my_ads_name:$DATA
     *
     * ADS name from FindNextStreamW() always has ":$DATA" string at the end.
     *
     * This function copies ADS name to namebuf.
     */

    p1 = strchr(full_ads_name, ':');
    if (p1 == NULL) {
        return -1;
    }

    p2 = strchr(p1 + 1, ':');
    if (p2 == NULL) {
        return -1;
    }

    /* skip empty ads name */
    if (p2 - p1 == 1) {
        return 0;
    }

    if (p2 - p1 + 1 > namebuf_len) {
        return -1;
    }

    memcpy(namebuf, p1 + 1, p2 - p1 - 1);
    namebuf[p2 - p1 - 1] = '\0';

    return p2 - p1;
}

/*
 * get_full_path_win32 - get full file name base on a handle
 *
 * This function gets full file name based on a handle specified by <fd> to
 * a file or directory.
 *
 * Caller function needs to free the file name string after use.
 */
char *get_full_path_win32(HANDLE hDir, const char *name)
{
    g_autofree char *full_file_name = NULL;
    DWORD total_size;
    DWORD name_size;

    if (hDir == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    full_file_name = g_malloc0(NAME_MAX);

    /* get parent directory full file name */
    name_size = GetFinalPathNameByHandle(hDir, full_file_name,
                                         NAME_MAX - 1, FILE_NAME_NORMALIZED);
    if (name_size == 0 || name_size > NAME_MAX - 1) {
        return NULL;
    }

    /* full path returned is the "\\?\" syntax, remove the lead string */
    memmove(full_file_name, full_file_name + 4, NAME_MAX - 4);

    if (name != NULL) {
        total_size = strlen(full_file_name) + strlen(name) + 2;

        if (total_size > NAME_MAX) {
            return NULL;
        }

        /* build sub-directory file name */
        strcat(full_file_name, "\\");
        strcat(full_file_name, name);
    }

    return g_steal_pointer(&full_file_name);
}

/*
 * fgetxattr_win32 - get extended attribute by fd
 *
 * This function gets extened attribute by <fd>. <fd> will be translated to
 * Windows handle.
 *
 * This function emulates extended attribute by NTFS ADS.
 */
ssize_t fgetxattr_win32(int fd, const char *name, void *value, size_t size)
{
    g_autofree char *full_file_name = NULL;
    char ads_file_name[NAME_MAX + 1] = {0};
    DWORD dwBytesRead;
    HANDLE hStream;
    HANDLE hFile;

    hFile = (HANDLE)_get_osfhandle(fd);

    full_file_name = get_full_path_win32(hFile, NULL);
    if (full_file_name == NULL) {
        errno = EIO;
        return -1;
    }

    if (build_ads_name(ads_file_name, NAME_MAX, full_file_name, name) < 0) {
        errno = EIO;
        return -1;
    }

    hStream = CreateFile(ads_file_name, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hStream == INVALID_HANDLE_VALUE &&
        GetLastError() == ERROR_FILE_NOT_FOUND) {
        errno = ENODATA;
        return -1;
    }

    if (ReadFile(hStream, value, size, &dwBytesRead, NULL) == FALSE) {
        errno = EIO;
        CloseHandle(hStream);
        return -1;
    }

    CloseHandle(hStream);

    return dwBytesRead;
}

/*
 * openat_win32 - emulate openat()
 *
 * This function emulates openat().
 *
 * this function needs a handle to get the full file name, it has to
 * convert fd to handle by get_osfhandle().
 *
 * For symbolic access:
 * 1. Parent directory handle <dirfd> should not be a symbolic link because
 *    it is opened by openat_dir() which can prevent from opening a link to
 *    a dirctory.
 * 2. Link flag in <mode> is not set because Windows does not have this flag.
 *    Create a new symbolic link will be denied.
 * 3. This function checks file symbolic link attribute after open.
 *
 * So native symbolic link will not be accessed by 9p client.
 */
int openat_win32(int dirfd, const char *pathname, int flags, mode_t mode)
{
    g_autofree char *full_file_name1 = NULL;
    g_autofree char *full_file_name2 = NULL;
    HANDLE hFile;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);
    int fd;

    full_file_name1 = get_full_path_win32(hDir, pathname);
    if (full_file_name1 == NULL) {
        return -1;
    }

    fd = open(full_file_name1, flags, mode);
    if (fd > 0) {
        DWORD attribute;
        hFile = (HANDLE)_get_osfhandle(fd);

        full_file_name2 = get_full_path_win32(hFile, NULL);
        attribute = GetFileAttributes(full_file_name2);

        /* check if it is a symbolic link */
        if ((attribute == INVALID_FILE_ATTRIBUTES)
            || (attribute & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            errno = EACCES;
            close(fd);
        }
    }

    return fd;
}

/*
 * fstatat_win32 - emulate fstatat()
 *
 * This function emulates fstatat().
 *
 * Access to a symbolic link will be denied to prevent security issues.
 */
int fstatat_win32(int dirfd, const char *pathname,
                  struct stat *statbuf, int flags)
{
    g_autofree char *full_file_name = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);
    BY_HANDLE_FILE_INFORMATION file_info;
    DWORD attribute;
    int err = 0;
    int ret = -1;
    ino_t st_ino;

    full_file_name = get_full_path_win32(hDir, pathname);
    if (full_file_name == NULL) {
        return ret;
    }

    /* open file to lock it */
    hFile = CreateFile(full_file_name, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS
                       | FILE_FLAG_OPEN_REPARSE_POINT,
                       NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        err = EACCES;
        goto out;
    }

    attribute = GetFileAttributes(full_file_name);

    /* check if it is a symbolic link */
    if ((attribute == INVALID_FILE_ATTRIBUTES)
        || (attribute & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        errno = EACCES;
        goto out;
    }

    ret = stat(full_file_name, statbuf);

    if (GetFileInformationByHandle(hFile, &file_info) == 0) {
        errno = EACCES;
        goto out;
    }

    /*
     * Windows (NTFS) file ID is a 64-bit ID:
     *   16-bit sequence ID + 48 bit segment number
     *
     * But currently, ino_t defined in Windows header file is only 16-bit,
     * and it is not patched by MinGW. So we build a pseudo inode number
     * by the low 32-bit segment number when ino_t is only 16-bit.
     */
    if (sizeof(st_ino) == sizeof(uint64_t)) {
        st_ino = (ino_t)((uint64_t)file_info.nFileIndexLow
                         | (((uint64_t)file_info.nFileIndexHigh) << 32));
    } else if (sizeof(st_ino) == sizeof(uint16_t)) {
        st_ino = (ino_t)(((uint16_t)file_info.nFileIndexLow)
                         ^ ((uint16_t)(file_info.nFileIndexLow >> 16)));
    } else {
        st_ino = (ino_t)file_info.nFileIndexLow;
    }

    statbuf->st_ino = st_ino;

out:
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }

    if (err != 0) {
        errno = err;
    }
    return ret;
}

/*
 * mkdirat_win32 - emulate mkdirat()
 *
 * This function emulates mkdirat().
 *
 * this function needs a handle to get the full file name, it has to
 * convert fd to handle by get_osfhandle().
 */
int mkdirat_win32(int dirfd, const char *pathname, mode_t mode)
{
    g_autofree char *full_file_name = NULL;
    int ret = -1;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);

    full_file_name = get_full_path_win32(hDir, pathname);
    if (full_file_name == NULL) {
        return ret;
    }

    ret = mkdir(full_file_name);

    return ret;
}

/*
 * renameat_win32 - emulate renameat()
 *
 * This function emulates renameat().
 *
 * this function needs a handle to get the full file name, it has to
 * convert fd to handle by get_osfhandle().
 *
 * Access to a symbolic link will be denied to prevent security issues.
 */
int renameat_win32(int olddirfd, const char *oldpath,
                   int newdirfd, const char *newpath)
{
    g_autofree char *full_old_name = NULL;
    g_autofree char *full_new_name = NULL;
    HANDLE hFile;
    HANDLE hOldDir = (HANDLE)_get_osfhandle(olddirfd);
    HANDLE hNewDir = (HANDLE)_get_osfhandle(newdirfd);
    DWORD attribute;
    int err = 0;
    int ret = -1;

    full_old_name = get_full_path_win32(hOldDir, oldpath);
    full_new_name = get_full_path_win32(hNewDir, newpath);
    if (full_old_name == NULL || full_new_name == NULL) {
        return ret;
    }

    /* open file to lock it */
    hFile = CreateFile(full_old_name, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    attribute = GetFileAttributes(full_old_name);

    /* check if it is a symbolic link */
    if ((attribute == INVALID_FILE_ATTRIBUTES)
        || (attribute & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        err = EACCES;
        goto out;
    }

    CloseHandle(hFile);

    ret = rename(full_old_name, full_new_name);
out:
    if (err != 0) {
        errno = err;
    }
    return ret;
}

/*
 * utimensat_win32 - emulate utimensat()
 *
 * This function emulates utimensat().
 *
 * this function needs a handle to get the full file name, it has to
 * convert fd to handle by get_osfhandle().
 *
 * Access to a symbolic link will be denied to prevent security issues.
 */
int utimensat_win32(int dirfd, const char *pathname,
                    const struct timespec times[2], int flags)
{
    g_autofree char *full_file_name = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);
    DWORD attribute;
    struct utimbuf tm;
    int err = 0;
    int ret = -1;

    full_file_name = get_full_path_win32(hDir, pathname);
    if (full_file_name == NULL) {
        return ret;
    }

    /* open file to lock it */
    hFile = CreateFile(full_file_name, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS
                       | FILE_FLAG_OPEN_REPARSE_POINT,
                       NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        err = EACCES;
        goto out;
    }

    attribute = GetFileAttributes(full_file_name);

    /* check if it is a symbolic link */
    if ((attribute == INVALID_FILE_ATTRIBUTES)
        || (attribute & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        errno = EACCES;
        goto out;
    }

    tm.actime = times[0].tv_sec;
    tm.modtime = times[1].tv_sec;

    ret = utime(full_file_name, &tm);

out:
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }

    if (err != 0) {
        errno = err;
    }
    return ret;
}

/*
 * unlinkat_win32 - emulate unlinkat()
 *
 * This function emulates unlinkat().
 *
 * this function needs a handle to get the full file name, it has to
 * convert fd to handle by get_osfhandle().
 *
 * Access to a symbolic link will be denied to prevent security issues.
 */

int unlinkat_win32(int dirfd, const char *pathname, int flags)
{
    g_autofree char *full_file_name = NULL;
    HANDLE hFile;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);
    DWORD attribute;
    int err = 0;
    int ret = -1;

    full_file_name = get_full_path_win32(hDir, pathname);
    if (full_file_name == NULL) {
        return ret;
    }

    /*
     * open file to prevent other one modify it. FILE_SHARE_DELETE flag
     * allows remove a file even it is still in opening.
     */
    hFile = CreateFile(full_file_name, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    attribute = GetFileAttributes(full_file_name);

    /* check if it is a symbolic link */
    if ((attribute == INVALID_FILE_ATTRIBUTES)
        || (attribute & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        err = EACCES;
        goto out;
    }

    if (flags == AT_REMOVEDIR) { /* remove directory */
        if ((attribute & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            err = ENOTDIR;
            goto out;
        }
        ret = rmdir(full_file_name);
    } else { /* remove regular file */
        if ((attribute & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            err = EISDIR;
            goto out;
        }
        ret = remove(full_file_name);
    }

    /* after last handle closed, file will be removed */
    CloseHandle(hFile);

out:
    if (err != 0) {
        errno = err;
    }
    return ret;
}

/*
 * statfs_win32 - statfs() on Windows
 *
 * This function emulates statfs() on Windows host.
 */
int statfs_win32(const char *path, struct statfs *stbuf)
{
    char RealPath[4] = { 0 };
    unsigned long SectorsPerCluster;
    unsigned long BytesPerSector;
    unsigned long NumberOfFreeClusters;
    unsigned long TotalNumberOfClusters;

    /* only need first 3 bytes, e.g. "C:\ABC", only need "C:\" */
    memcpy(RealPath, path, 3);

    if (GetDiskFreeSpace(RealPath, &SectorsPerCluster, &BytesPerSector,
                         &NumberOfFreeClusters, &TotalNumberOfClusters) == 0) {
        errno = EIO;
        return -1;
    }

    stbuf->f_type = V9FS_MAGIC;
    stbuf->f_bsize =
        (__fsword_t)SectorsPerCluster * (__fsword_t)BytesPerSector;
    stbuf->f_blocks = (fsblkcnt_t)TotalNumberOfClusters;
    stbuf->f_bfree = (fsblkcnt_t)NumberOfFreeClusters;
    stbuf->f_bavail = (fsblkcnt_t)NumberOfFreeClusters;
    stbuf->f_files = -1;
    stbuf->f_ffree = -1;
    stbuf->f_namelen = NAME_MAX;
    stbuf->f_frsize = 0;
    stbuf->f_flags = 0;

    return 0;
}

/*
 * openat_dir - emulate openat_dir()
 *
 * This function emulates openat_dir().
 *
 * Access to a symbolic link will be denied to prevent security issues.
 */
int openat_dir(int dirfd, const char *name)
{
    g_autofree char *full_file_name = NULL;
    HANDLE hSubDir;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);
    DWORD attribute;

    full_file_name = get_full_path_win32(hDir, name);
    if (full_file_name == NULL) {
        return -1;
    }

    attribute = GetFileAttributes(full_file_name);
    if (attribute == INVALID_FILE_ATTRIBUTES) {
        return -1;
    }

    /* check if it is a directory */
    if ((attribute & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return -1;
    }

    /* do not allow opening a symbolic link */
    if ((attribute & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return -1;
    }

    /* open it */
    hSubDir = CreateFile(full_file_name, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    return _open_osfhandle((intptr_t)hSubDir, _O_RDONLY);
}


int openat_file(int dirfd, const char *name, int flags, mode_t mode)
{
    return openat_win32(dirfd, name, flags | _O_BINARY, mode);
}

/*
 * fgetxattrat_nofollow - get extended attribute
 *
 * This function gets extended attribute from file <path> in the directory
 * specified by <dirfd>. The extended atrribute name is specified by <name>
 * and return value will be put in <value>.
 *
 * This function emulates extended attribute by NTFS ADS.
 */
ssize_t fgetxattrat_nofollow(int dirfd, const char *path,
                             const char *name, void *value, size_t size)
{
    g_autofree char *full_file_name = NULL;
    char ads_file_name[NAME_MAX + 1] = { 0 };
    DWORD dwBytesRead;
    HANDLE hStream;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);

    full_file_name = get_full_path_win32(hDir, path);
    if (full_file_name == NULL) {
        errno = EIO;
        return -1;
    }

    if (build_ads_name(ads_file_name, NAME_MAX, full_file_name, name) < 0) {
        errno = EIO;
        return -1;
    }

    hStream = CreateFile(ads_file_name, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hStream == INVALID_HANDLE_VALUE &&
        GetLastError() == ERROR_FILE_NOT_FOUND) {
        errno = ENODATA;
        return -1;
    }

    if (ReadFile(hStream, value, size, &dwBytesRead, NULL) == FALSE) {
        errno = EIO;
        CloseHandle(hStream);
        return -1;
    }

    CloseHandle(hStream);

    return dwBytesRead;
}

/*
 * fsetxattrat_nofollow - set extended attribute
 *
 * This function set extended attribute to file <path> in the directory
 * specified by <dirfd>.
 *
 * This function emulates extended attribute by NTFS ADS.
 */

int fsetxattrat_nofollow(int dirfd, const char *path, const char *name,
                         void *value, size_t size, int flags)
{
    g_autofree char *full_file_name = NULL;
    char ads_file_name[NAME_MAX + 1] = { 0 };
    DWORD dwBytesWrite;
    HANDLE hStream;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);

    full_file_name = get_full_path_win32(hDir, path);
    if (full_file_name == NULL) {
        errno = EIO;
        return -1;
    }

    if (build_ads_name(ads_file_name, NAME_MAX, full_file_name, name) < 0) {
        errno = EIO;
        return -1;
    }

    hStream = CreateFile(ads_file_name, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hStream == INVALID_HANDLE_VALUE) {
        errno = EIO;
        return -1;
    }

    if (WriteFile(hStream, value, size, &dwBytesWrite, NULL) == FALSE) {
        errno = EIO;
        CloseHandle(hStream);
        return -1;
    }

    CloseHandle(hStream);

    return 0;
}

/*
 * flistxattrat_nofollow - list extended attribute
 *
 * This function gets extended attribute lists from file <filename> in the
 * directory specified by <dirfd>. Lists returned will be put in <list>.
 *
 * This function emulates extended attribute by NTFS ADS.
 */
ssize_t flistxattrat_nofollow(int dirfd, const char *filename,
                              char *list, size_t size)
{
    g_autofree char *full_file_name = NULL;
    WCHAR WideCharStr[NAME_MAX + 1] = { 0 };
    char full_ads_name[NAME_MAX + 1];
    WIN32_FIND_STREAM_DATA fsd;
    BOOL bFindNext;
    char *list_ptr = list;
    size_t list_left_size = size;
    HANDLE hFind;
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);
    int ret;

    full_file_name = get_full_path_win32(hDir, filename);
    if (full_file_name == NULL) {
        errno = EIO;
        return -1;
    }

    /*
     * ADS enumerate function only has WCHAR version, so we need to
     * covert filename to utf-8 string.
     */
    ret = MultiByteToWideChar(CP_UTF8, 0, full_file_name,
                              strlen(full_file_name), WideCharStr, NAME_MAX);
    if (ret == 0) {
        errno = EIO;
        return -1;
    }

    hFind = FindFirstStreamW(WideCharStr, FindStreamInfoStandard, &fsd, 0);
    if (hFind == INVALID_HANDLE_VALUE) {
        errno = ENODATA;
        return -1;
    }

    do {
        memset(full_ads_name, 0, sizeof(full_ads_name));

        /*
         * ADS enumerate function only has WCHAR version, so we need to
         * covert cStreamName to utf-8 string.
         */
        ret = WideCharToMultiByte(CP_UTF8, 0,
                                  fsd.cStreamName, wcslen(fsd.cStreamName) + 1,
                                  full_ads_name, sizeof(full_ads_name) - 1,
                                  NULL, NULL);
        if (ret == 0) {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                errno = ERANGE;
            }
            CloseHandle(hFind);
            return -1;
        }

        ret = copy_ads_name(list_ptr, list_left_size, full_ads_name);
        if (ret < 0) {
            errno = ERANGE;
            CloseHandle(hFind);
            return -1;
        }

        list_ptr = list_ptr + ret;
        list_left_size = list_left_size - ret;

        bFindNext = FindNextStreamW(hFind, &fsd);
    } while (bFindNext);

    CloseHandle(hFind);

    return size - list_left_size;
}

/*
 * fremovexattrat_nofollow - remove extended attribute
 *
 * This function removes an extended attribute from file <filename> in the
 * directory specified by <dirfd>.
 *
 * This function emulates extended attribute by NTFS ADS.
 */
ssize_t fremovexattrat_nofollow(int dirfd, const char *filename,
                                const char *name)
{
    g_autofree char *full_file_name = NULL;
    char ads_file_name[NAME_MAX + 1] = { 0 };
    HANDLE hDir = (HANDLE)_get_osfhandle(dirfd);

    full_file_name = get_full_path_win32(hDir, filename);
    if (full_file_name == NULL) {
        errno = EIO;
        return -1;
    }

    if (build_ads_name(ads_file_name, NAME_MAX, filename, name) < 0) {
        errno = EIO;
        return -1;
    }

    if (DeleteFile(ads_file_name) != 0) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            errno = ENODATA;
            return -1;
        }
    }

    return 0;
}

/*
 * local_opendir_nofollow - open a Windows directory
 *
 * This function returns a fd of the directory specified by
 * <dirpath> based on 9pfs mount point.
 *
 * Windows POSIX API does not support opening a directory by open(). Only
 * handle of directory can be opened by CreateFile().
 * This function convert handle to fd by _open_osfhandle().
 *
 * This function checks the resolved path of <dirpath>. If the resolved
 * path is not in the scope of root directory (e.g. by symbolic link), then
 * this function will fail to prevent any security issues.
 */
int local_opendir_nofollow(FsContext *fs_ctx, const char *dirpath)
{
    g_autofree char *full_file_name = NULL;
    LocalData *data = fs_ctx->private;
    HANDLE hDir;
    int dirfd;

    dirfd = openat_dir(data->mountfd, dirpath);
    if (dirfd == -1) {
        return -1;
    }
    hDir = (HANDLE)_get_osfhandle(dirfd);

    full_file_name = get_full_path_win32(hDir, NULL);
    if (full_file_name == NULL) {
        close(dirfd);
        return -1;
    }

    /*
     * Check if the resolved path is in the root directory scope:
     * data->root_path and full_file_name are full path with symbolic
     * link resolved, so fs_ctx->root_path must be in the head of
     * full_file_name. If not, that means guest OS tries to open a file not
     * in the scope of mount point. This operation should be denied.
     */
    if (memcmp(full_file_name, data->root_path,
               strlen(data->root_path)) != 0) {
        close(dirfd);
        return -1;
    }

    return dirfd;
}

/*
 * qemu_mknodat - mknodat emulate function
 *
 * This function emulates mknodat on Windows. It only works when security
 * model is mapped or mapped-xattr.
 */
int qemu_mknodat(int dirfd, const char *filename, mode_t mode, dev_t dev)
{
    if (S_ISREG(mode) || !(mode & S_IFMT)) {
        int fd = openat_file(dirfd, filename, O_CREAT, mode);
        if (fd == -1) {
            return -1;
        }
        close_preserve_errno(fd);
        return 0;
    }

    error_report_once("Unsupported operation for mknodat");
    errno = ENOTSUP;
    return -1;
}
