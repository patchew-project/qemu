/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef SYS_openat2
#include <linux/openat2.h>

static int cmdline_argc;
static char **cmdline_argv;

static void check(int dirfd, const char *name, uint64_t flags,
                  uint64_t resolve, int expected_errno)
{
    struct open_how how = { .flags = flags, .resolve = resolve };
    char byte;
    int fd = syscall(SYS_openat2, dirfd, name, &how, sizeof(how));

    if (expected_errno) {
        assert(fd == -1 && errno == expected_errno);
        return;
    }
    assert(fd >= 0);
    assert(fcntl(fd, F_GETFD) == FD_CLOEXEC);
    if (flags & O_PATH) {
        assert(fcntl(fd, F_GETFL) & O_PATH);
        assert(read(fd, &byte, 1) == -1 && errno == EBADF);
    } else {
        for (int i = 0; i < cmdline_argc; i++) {
            const char *p = cmdline_argv[i];

            do {
                assert(read(fd, &byte, 1) == 1 && byte == *p);
            } while (*p++);
        }
        assert(read(fd, &byte, 1) == 0);
        assert(write(fd, "x", 1) == -1 && errno == EBADF);
    }
    assert(close(fd) == 0);
}
#endif

int main(int argc, char **argv)
{
#ifdef SYS_openat2
    struct open_how how = { .flags = O_RDONLY | O_CLOEXEC };
    int fd = syscall(SYS_openat2, AT_FDCWD, "/proc/self/cmdline",
                     &how, sizeof(how));
    int proc, self, root, flags = O_RDONLY | O_CLOEXEC;

    cmdline_argc = argc;
    cmdline_argv = argv;
    if (fd < 0 && errno == ENOSYS) {
        return EXIT_SUCCESS;
    }
    assert(fd >= 0 && close(fd) == 0);
    proc = open("/proc", O_PATH | O_DIRECTORY | O_CLOEXEC);
    self = open("/proc/self", O_PATH | O_DIRECTORY | O_CLOEXEC);
    root = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    assert(proc >= 0 && self >= 0 && root >= 0);
    check(-1, "/proc/self/cmdline", flags, 0, 0);
    check(AT_FDCWD, "/proc/self/cmdline", flags,
          RESOLVE_NO_SYMLINKS, ELOOP);
    check(proc, "self/cmdline", flags, 0, 0);
    check(proc, "self/cmdline", flags, RESOLVE_NO_SYMLINKS, ELOOP);
    check(proc, "self/root/proc/self/cmdline", flags,
          RESOLVE_NO_MAGICLINKS, ELOOP);
    check(self, "../self/cmdline", flags, RESOLVE_BENEATH, EXDEV);
    check(self, "../self/cmdline", flags, RESOLVE_IN_ROOT, ENOENT);
    check(root, "proc/self/cmdline", flags, RESOLVE_NO_XDEV, EXDEV);
    check(proc, "self/cmdline", flags, UINT64_C(1) << 63, EINVAL);
    check(self, "cmdline", O_PATH | O_CLOEXEC, 0, 0);
    assert(close(root) == 0);
    assert(close(self) == 0);
    assert(close(proc) == 0);
#endif
    return EXIT_SUCCESS;
}
