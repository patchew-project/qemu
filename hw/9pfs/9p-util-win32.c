/*
 * 9p utilities (Windows Implementation)
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "9p.h"
#include "9p-util.h"
#include "9p-linux-errno.h"
#include <windows.h>
#include <dirent.h>

#ifndef V9FS_MAGIC
#define V9FS_MAGIC 0x53465039 /* string "9PFS" */
#endif

struct translate_map {
    int output;     /* Linux error number */
    int input;      /* Windows error number */
};

static int build_ads_name(char *namebuf, size_t namebuflen,
                          const char *dirname, const char *filename,
                          const char *ads_name)
{
    size_t totalsize;

    totalsize = strlen(dirname) + strlen(filename) + strlen(ads_name) + 3;
    if (totalsize  > namebuflen) {
        return -1;
    }

    /*
     * NTFS ADS (Alternate Data Streams) name format:
     *  filename:ads_name
     * e.g.
     *  d:\1.txt:my_ads_name
     */
    strcpy(namebuf, dirname);
    strcat(namebuf, "\\");
    strcat(namebuf, filename);
    strcat(namebuf, ":");
    strcat(namebuf, ads_name);

    return 0;
}

static ssize_t copy_ads_name(char *namebuf, size_t namebuflen,
                             char *fulladsname)
{
    char *p1, *p2;

    /*
     * NTFS ADS (Alternate Data Streams) name from emurate data format:
     *  :ads_name:$DATA
     * e.g.
     *  :my_ads_name:$DATA
     *
     * ADS name from FindNextStreamW() always have ":$DATA" string at the end
     *
     * This function copy ADS name to namebuf.
     */

    p1 = strchr(fulladsname, ':');
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

    if (p2 - p1 + 1 > namebuflen) {
        return -1;
    }

    memcpy(namebuf, p1 + 1, p2 - p1 - 1);
    namebuf[p2 - p1 - 1] = '\0';

    return p2 - p1;
}

ssize_t fgetxattrat_nofollow(const char *dirname, const char *filename,
                             const char *name, void *value, size_t size)
{
    HANDLE hStream;
    char ADSFileName[NAME_MAX + 1] = {0};
    DWORD dwBytesRead;

    if (build_ads_name(ADSFileName, NAME_MAX, dirname, filename, name) < 0) {
        errno = EIO;
        return -1;
    }

    hStream = CreateFile(ADSFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
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

ssize_t flistxattrat_nofollow(const char *dirname, const char *filename,
                              char *list, size_t size)
{
    WCHAR WideCharStr[NAME_MAX + 1] = { 0 };
    char fulladsname[NAME_MAX + 1];
    char *full_fs_name = merge_fs_path(dirname, filename);
    int ret;
    HANDLE hFind;
    WIN32_FIND_STREAM_DATA fsd;
    BOOL bFindNext;
    char *listptr = list;
    size_t listleftsize = size;

    /*
     * ADS emurate function only have WCHAR version, need to  covert filename
     * to WCHAR string.
     */

    ret = MultiByteToWideChar(CP_UTF8, 0, full_fs_name,
                              strlen(full_fs_name), WideCharStr, NAME_MAX);
    g_free(full_fs_name);
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
        memset(fulladsname, 0, sizeof(fulladsname));

        /*
         * ADS emurate function only have WCHAR version, need to covert
         * cStreamName to utf-8 string.
         */

        ret = WideCharToMultiByte(CP_UTF8, 0,
                                  fsd.cStreamName, wcslen(fsd.cStreamName) + 1,
                                  fulladsname, sizeof(fulladsname) - 1,
                                  NULL, NULL);

        if (ret == 0) {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                errno = ERANGE;
            }
            CloseHandle(hFind);
            return -1;
        }

        ret = copy_ads_name(listptr, listleftsize, fulladsname);
        if (ret < 0) {
            errno = ERANGE;
            CloseHandle(hFind);
            return -1;
        }

        listptr = listptr + ret;
        listleftsize = listleftsize - ret;

        bFindNext = FindNextStreamW(hFind, &fsd);
    } while (bFindNext);

    CloseHandle(hFind);

    return size - listleftsize;
}

ssize_t fremovexattrat_nofollow(const char *dirname, const char *filename,
                                const char *name)
{
    char ADSFileName[NAME_MAX + 1] = {0};

    if (build_ads_name(ADSFileName, NAME_MAX, dirname, filename, name) < 0) {
        errno = EIO;
        return -1;
    }

    if (DeleteFile(ADSFileName) != 0) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            errno = ENODATA;
            return -1;
        }
    }

    return 0;
}

int fsetxattrat_nofollow(const char *dirname, const char *filename,
                         const char *name, void *value, size_t size, int flags)
{
    HANDLE hStream;
    char ADSFileName[NAME_MAX + 1] = {0};
    DWORD dwBytesWrite;

    if (build_ads_name(ADSFileName, NAME_MAX, dirname, filename, name) < 0) {
        errno = EIO;
        return -1;
    }

    hStream = CreateFile(ADSFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL,
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

int qemu_mknodat(const char *dirname, const char *filename,
                 mode_t mode, dev_t dev)
{
    errno = ENOTSUP;
    return -1;
}

int qemu_statfs(const char *fs_root, struct statfs *stbuf)
{
    HANDLE hFile;
    char RealPath[NAME_MAX + 1];
    unsigned long SectorsPerCluster;
    unsigned long BytesPerSector;
    unsigned long NumberOfFreeClusters;
    unsigned long TotalNumberOfClusters;

    hFile = CreateFile(fs_root, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        errno = EIO;
        return -1;
    }

    /* get real path of root */
    if (GetFinalPathNameByHandle(hFile, RealPath, sizeof(RealPath),
                                 FILE_NAME_NORMALIZED) == 0) {
        errno = EIO;
        CloseHandle(hFile);
        return -1;
    }

    CloseHandle(hFile);

    /*
     * GetFinalPathNameByHandle will return real path start with "\\\\?\\".
     * "C:\\123" will be "\\\\?\\C:\\123"
     * Skip first 4 bytes and truncate the string at offset 7, it will get
     * the real root directory like "C:\\", this is parameter GetDiskFreeSpace
     * needed.
     */

    RealPath[7] = '\0';

    if (GetDiskFreeSpace(RealPath + 4, &SectorsPerCluster, &BytesPerSector,
                         &NumberOfFreeClusters, &TotalNumberOfClusters) == 0) {
        errno = EIO;
        return -1;
    }

    stbuf->f_type = V9FS_MAGIC;
    stbuf->f_bsize = (__fsword_t)(SectorsPerCluster * BytesPerSector);
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

int errno_translate_win32(int errno_win32)
    {
    unsigned int i;

    /*
     * The translation table only contains values which could be returned
     * as a result of a filesystem operation, i.e. network/socket related
     * errno values need not be considered for translation.
     */
    static struct translate_map errno_map[] = {
        /* Linux errno          Windows errno   */
        { L_EDEADLK,            EDEADLK         },
        { L_ENAMETOOLONG,       ENAMETOOLONG    },
        { L_ENOLCK,             ENOLCK          },
        { L_ENOSYS,             ENOSYS          },
        { L_ENOTEMPTY,          ENOTEMPTY       },
        { L_EILSEQ,             EILSEQ          },
        { L_ELOOP,              ELOOP           },
    };

    /* scan errno_win32 table for a matching Linux errno value */

    for (i = 0; i < sizeof(errno_map) / sizeof(errno_map[0]); i++) {
        if (errno_win32 == errno_map[i].input) {
            return errno_map[i].output;
        }
    }

    /* no translation necessary */

    return errno_win32;
    }
