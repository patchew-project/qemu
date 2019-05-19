/*
 *  Linux file-related syscall implementations
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

SYSCALL_IMPL(close)
{
    int fd = arg1;

    fd_trans_unregister(fd);
    return get_errno(close(fd));
}

/*
 * Helpers for do_openat, manipulating /proc/self/foo.
 */

static int open_self_cmdline(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    struct linux_binprm *bprm = ((TaskState *)cpu->opaque)->bprm;
    int i;

    for (i = 0; i < bprm->argc; i++) {
        size_t len = strlen(bprm->argv[i]) + 1;

        if (write(fd, bprm->argv[i], len) != len) {
            return -1;
        }
    }

    return 0;
}

static int open_self_maps(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    TaskState *ts = cpu->opaque;
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/self/maps", "r");
    if (fp == NULL) {
        return -1;
    }

    while ((read = getline(&line, &len, fp)) != -1) {
        int fields, dev_maj, dev_min, inode;
        uint64_t min, max, offset;
        char flag_r, flag_w, flag_x, flag_p;
        char path[512] = "";
        fields = sscanf(line, "%"PRIx64"-%"PRIx64" %c%c%c%c %"PRIx64" %x:%x %d"
                        " %512s", &min, &max, &flag_r, &flag_w, &flag_x,
                        &flag_p, &offset, &dev_maj, &dev_min, &inode, path);

        if ((fields < 10) || (fields > 11)) {
            continue;
        }
        if (h2g_valid(min)) {
            int flags = page_get_flags(h2g(min));
            max = h2g_valid(max - 1) ? max : (uintptr_t)g2h(GUEST_ADDR_MAX) + 1;
            if (page_check_range(h2g(min), max - min, flags) == -1) {
                continue;
            }
            if (h2g(min) == ts->info->stack_limit) {
                pstrcpy(path, sizeof(path), "      [stack]");
            }
            dprintf(fd, TARGET_ABI_FMT_ptr "-" TARGET_ABI_FMT_ptr
                    " %c%c%c%c %08" PRIx64 " %02x:%02x %d %s%s\n",
                    h2g(min), h2g(max - 1) + 1, flag_r, flag_w,
                    flag_x, flag_p, offset, dev_maj, dev_min, inode,
                    path[0] ? "         " : "", path);
        }
    }

    free(line);
    fclose(fp);

    return 0;
}

static int open_self_stat(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    TaskState *ts = cpu->opaque;
    abi_ulong start_stack = ts->info->start_stack;
    int i;

    for (i = 0; i < 44; i++) {
        char buf[128];
        int len;
        uint64_t val = 0;

        if (i == 0) {
            /* pid */
            val = getpid();
            snprintf(buf, sizeof(buf), "%"PRId64 " ", val);
        } else if (i == 1) {
            /* app name */
            snprintf(buf, sizeof(buf), "(%s) ", ts->bprm->argv[0]);
        } else if (i == 27) {
            /* stack bottom */
            val = start_stack;
            snprintf(buf, sizeof(buf), "%"PRId64 " ", val);
        } else {
            /* for the rest, there is MasterCard */
            snprintf(buf, sizeof(buf), "0%c", i == 43 ? '\n' : ' ');
        }

        len = strlen(buf);
        if (write(fd, buf, len) != len) {
            return -1;
        }
    }

    return 0;
}

static int open_self_auxv(void *cpu_env, int fd)
{
    CPUState *cpu = ENV_GET_CPU((CPUArchState *)cpu_env);
    TaskState *ts = cpu->opaque;
    abi_ulong auxv = ts->info->saved_auxv;
    abi_ulong len = ts->info->auxv_len;
    char *ptr;

    /*
     * Auxiliary vector is stored in target process stack.
     * read in whole auxv vector and copy it to file
     */
    ptr = lock_user(VERIFY_READ, auxv, len, 0);
    if (ptr != NULL) {
        while (len > 0) {
            ssize_t r;
            r = write(fd, ptr, len);
            if (r <= 0) {
                break;
            }
            len -= r;
            ptr += r;
        }
        lseek(fd, 0, SEEK_SET);
        unlock_user(ptr, auxv, len);
    }

    return 0;
}

static int is_proc_myself(const char *filename, const char *entry)
{
    if (!strncmp(filename, "/proc/", strlen("/proc/"))) {
        filename += strlen("/proc/");
        if (!strncmp(filename, "self/", strlen("self/"))) {
            filename += strlen("self/");
        } else if (*filename >= '1' && *filename <= '9') {
            char myself[80];
            snprintf(myself, sizeof(myself), "%d/", getpid());
            if (!strncmp(filename, myself, strlen(myself))) {
                filename += strlen(myself);
            } else {
                return 0;
            }
        } else {
            return 0;
        }
        if (!strcmp(filename, entry)) {
            return 1;
        }
    }
    return 0;
}

#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
static int is_proc(const char *filename, const char *entry)
{
    return strcmp(filename, entry) == 0;
}

static int open_net_route(void *cpu_env, int fd)
{
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        return -1;
    }

    /* read header */

    read = getline(&line, &len, fp);
    dprintf(fd, "%s", line);

    /* read routes */

    while ((read = getline(&line, &len, fp)) != -1) {
        char iface[16];
        uint32_t dest, gw, mask;
        unsigned int flags, refcnt, use, metric, mtu, window, irtt;
        int fields;

        fields = sscanf(line,
                        "%s\t%08x\t%08x\t%04x\t%d\t%d\t%d\t%08x\t%d\t%u\t%u\n",
                        iface, &dest, &gw, &flags, &refcnt, &use, &metric,
                        &mask, &mtu, &window, &irtt);
        if (fields != 11) {
            continue;
        }
        dprintf(fd, "%s\t%08x\t%08x\t%04x\t%d\t%d\t%d\t%08x\t%d\t%u\t%u\n",
                iface, tswap32(dest), tswap32(gw), flags, refcnt, use,
                metric, tswap32(mask), mtu, window, irtt);
    }

    free(line);
    fclose(fp);

    return 0;
}
#endif

static abi_long do_openat(void *cpu_env, int dirfd, abi_ulong target_path,
                          int target_flags, mode_t mode)
{
    struct fake_open {
        const char *filename;
        int (*fill)(void *cpu_env, int fd);
        int (*cmp)(const char *s1, const char *s2);
    };
    static const struct fake_open fakes[] = {
        { "maps", open_self_maps, is_proc_myself },
        { "stat", open_self_stat, is_proc_myself },
        { "auxv", open_self_auxv, is_proc_myself },
        { "cmdline", open_self_cmdline, is_proc_myself },
#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
        { "/proc/net/route", open_net_route, is_proc },
#endif
    };

    char *pathname = lock_user_string(target_path);
    int flags = target_to_host_bitmask(target_flags, fcntl_flags_tbl);
    abi_long ret;
    size_t i;

    if (!pathname) {
        return -TARGET_EFAULT;
    }

    if (is_proc_myself(pathname, "exe")) {
        ret = qemu_getauxval(AT_EXECFD);
        if (ret == 0) {
            ret = get_errno(safe_openat(dirfd, exec_path, flags, mode));
        }
        goto done;
    }

    for (i = 0; i < ARRAY_SIZE(fakes); ++i) {
        const struct fake_open *fake_open = &fakes[i];
        const char *tmpdir;
        char filename[PATH_MAX];
        int fd;

        if (!fake_open->cmp(pathname, fake_open->filename)) {
            continue;
        }

        /* create temporary file to map stat to */
        tmpdir = getenv("TMPDIR");
        if (!tmpdir) {
            tmpdir = "/tmp";
        }
        snprintf(filename, sizeof(filename), "%s/qemu-open.XXXXXX", tmpdir);
        fd = mkstemp(filename);
        if (fd < 0) {
            ret = -TARGET_ENOENT;
            goto done;
        }
        unlink(filename);

        ret = fake_open->fill(cpu_env, fd);
        if (ret) {
            ret = get_errno(ret);
            close(fd);
            goto done;
        }
        lseek(fd, 0, SEEK_SET);
        ret = fd;
        goto done;
    }

    ret = get_errno(safe_openat(dirfd, path(pathname), flags, mode));
 done:
    fd_trans_unregister(ret);
    unlock_user(pathname, target_path, 0);
    return ret;
}

#ifdef TARGET_NR_open
SYSCALL_IMPL(open)
{
    return do_openat(cpu_env, AT_FDCWD, arg1, arg2, arg3);
}
#endif

SYSCALL_IMPL(openat)
{
    return do_openat(cpu_env, arg1, arg2, arg3, arg4);
}

SYSCALL_IMPL(read)
{
    int fd = arg1;
    abi_ulong target_p = arg2;
    abi_ulong size = arg3;
    void *p;
    abi_long ret;

    if (target_p == 0 && size == 0) {
        return get_errno(safe_read(fd, NULL, 0));
    }
    p = lock_user(VERIFY_WRITE, target_p, size, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(safe_read(fd, p, size));
    if (!is_error(ret)) {
        TargetFdDataFunc trans = fd_trans_host_to_target_data(fd);
        if (trans) {
            ret = trans(p, ret);
        }
    }
    unlock_user(p, target_p, ret);
    return ret;
}

static abi_long do_readlinkat(int dirfd, abi_ulong target_path,
                              abi_ulong target_buf, abi_ulong bufsiz)
{
    char *p = lock_user_string(target_path);
    void *buf = lock_user(VERIFY_WRITE, target_buf, bufsiz, 0);
    abi_long ret;

    if (!p || !buf) {
        ret = -TARGET_EFAULT;
    } else if (!bufsiz) {
        /* Short circuit this for the magic exe check. */
        ret = -TARGET_EINVAL;
    } else if (is_proc_myself((const char *)p, "exe")) {
        char real[PATH_MAX];
        char *temp = realpath(exec_path, real);

        if (temp == NULL) {
            ret = -host_to_target_errno(errno);
        } else {
            ret = MIN(strlen(real), bufsiz);
            /* We cannot NUL terminate the string. */
            memcpy(buf, real, ret);
        }
    } else {
        ret = get_errno(readlinkat(dirfd, path(p), buf, bufsiz));
    }
    unlock_user(buf, target_buf, ret);
    unlock_user(p, target_path, 0);
    return ret;
}

#ifdef TARGET_NR_readlink
SYSCALL_IMPL(readlink)
{
    return do_readlinkat(AT_FDCWD, arg1, arg2, arg3);
}
#endif

#ifdef TARGET_NR_readlinkat
SYSCALL_IMPL(readlinkat)
{
    return do_readlinkat(arg1, arg2, arg3, arg4);
}
#endif

SYSCALL_IMPL(write)
{
    int fd = arg1;
    abi_ulong target_p = arg2;
    abi_ulong size = arg3;
    TargetFdDataFunc trans;
    abi_long ret;
    void *p;

    if (target_p == 0 && size == 0) {
        return get_errno(safe_write(fd, NULL, 0));
    }
    p = lock_user(VERIFY_READ, target_p, size, 1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    trans = fd_trans_target_to_host_data(fd);
    if (trans) {
        void *copy = g_malloc(size);
        memcpy(copy, p, size);
        ret = trans(copy, size);
        if (ret >= 0) {
            ret = get_errno(safe_write(fd, copy, ret));
        }
        g_free(copy);
    } else {
        ret = get_errno(safe_write(fd, p, size));
    }
    unlock_user(p, target_p, 0);
    return ret;
}
