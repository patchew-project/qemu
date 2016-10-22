/*
 * Support for file backed by mmaped host memory.
 *
 * Authors:
 *  Rafael David Tinoco <rafael.tinoco@canonical.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/mmap-file.h"

static char *qemu_mmap_rand_name(void)
{
    char *name;
    GRand *rsufix;
    guint32 sufix;

    rsufix = g_rand_new();
    sufix = g_rand_int(rsufix);
    g_free(rsufix);
    name = g_strdup_printf("mmap-%u", sufix);

    return name;
}

static inline void qemu_mmap_rand_name_free(char *str)
{
    g_free(str);
}

static bool qemu_mmap_is(const char *path, mode_t what)
{
    struct stat s;

    memset(&s,  0, sizeof(struct stat));
    if (stat(path, &s)) {
        perror("stat");
        goto negative;
    }

    if ((s.st_mode & S_IFMT) == what) {
        return true;
    }

negative:
    return false;
}

static inline bool qemu_mmap_is_file(const char *path)
{
    return qemu_mmap_is(path, S_IFREG);
}

static inline bool qemu_mmap_is_dir(const char *path)
{
    return qemu_mmap_is(path, S_IFDIR);
}

static void *qemu_mmap_alloc_file(const char *filepath, size_t size, int *fd)
{
    void *ptr;
    int mfd = -1;

    *fd = -1;

    mfd = open(filepath, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (mfd == -1) {
        perror("open");
        return NULL;
    }

    unlink(filepath);

    if (ftruncate(mfd, size) == -1) {
        perror("ftruncate");
        close(mfd);
        return NULL;
    }

    ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        close(mfd);
        return NULL;
    }

    *fd = mfd;
    return ptr;
}

static void *qemu_mmap_alloc_dir(const char *dirpath, size_t size, int *fd)
{
    void *ptr;
    char *file, *rand, *tmp, *dir2use = NULL;

    if (dirpath && !qemu_mmap_is_dir(dirpath)) {
        return NULL;
    }

    tmp = (char *) g_get_tmp_dir();
    dir2use = dirpath ? (char *) dirpath : tmp;
    rand = qemu_mmap_rand_name();
    file = g_strdup_printf("%s/%s", dir2use, rand);
    ptr = qemu_mmap_alloc_file(file, size, fd);
    g_free(tmp);
    qemu_mmap_rand_name_free(rand);

    return ptr;
}

/*
 * "path" can be:
 *
 *   filename = full path for the file to back mmap
 *   dir path = full dir path where to create random file for mmap
 *   null     = will use <tmpdir>  to create random file for mmap
 */
void *qemu_mmap_alloc(const char *path, size_t size, int *fd)
{
    if (!path || qemu_mmap_is_dir(path)) {
        return qemu_mmap_alloc_dir(path, size, fd);
    }

    return qemu_mmap_alloc_file(path, size, fd);
}

void qemu_mmap_free(void *ptr, size_t size, int fd)
{
    if (ptr) {
        munmap(ptr, size);
    }

    if (fd != -1) {
        close(fd);
    }
}

bool qemu_mmap_check(const char *path)
{
    void *ptr;
    int fd = -1;
    bool r = true;

    ptr = qemu_mmap_alloc(path, 4096, &fd);
    if (!ptr) {
        r = false;
    }
    qemu_mmap_free(ptr, 4096, fd);

    return r == true ? true : false;
}
