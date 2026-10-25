/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static char expected[16384];
static size_t expected_size;

static void check(int dirfd, const char *name, int flags, int expected_errno)
{
    char actual[sizeof(expected)];
    size_t size = 0;
    int fd = openat(dirfd, name, flags);

    if (expected_errno) {
        assert(fd == -1 && errno == expected_errno);
        return;
    }
    assert(fd >= 0);
    assert((fcntl(fd, F_GETFD) & FD_CLOEXEC) ==
           ((flags & O_CLOEXEC) ? FD_CLOEXEC : 0));
    assert((fcntl(fd, F_GETFL) & (O_PATH | O_NOFOLLOW | O_ACCMODE)) ==
           (flags & (O_PATH | O_NOFOLLOW | O_ACCMODE)));
    if (flags & O_PATH) {
        assert(read(fd, actual, 1) == -1 && errno == EBADF);
    } else {
        for (;;) {
            ssize_t n = read(fd, actual + size, sizeof(actual) - size);

            if (n < 0 && errno == EINTR) {
                continue;
            }
            assert(n >= 0);
            if (!n) {
                break;
            }
            size += n;
            assert(size < sizeof(actual));
        }
        assert(size == expected_size && !memcmp(actual, expected, size));
        assert(write(fd, "x", 1) == -1 && errno == EBADF);
    }
    assert(close(fd) == 0);
}

int main(int argc, char **argv)
{
    struct rlimit saved, limit;
    int cwd = open(".", O_PATH | O_DIRECTORY | O_CLOEXEC);
    int proc = open("/proc", O_PATH | O_DIRECTORY | O_CLOEXEC);
    int self = open("/proc/self", O_PATH | O_DIRECTORY | O_CLOEXEC);
    int flags = O_RDONLY | O_CLOEXEC;

    assert(cwd >= 0 && proc >= 0 && self >= 0);
    for (int i = 0; i < argc; i++) {
        size_t size = strlen(argv[i]) + 1;

        assert(size <= sizeof(expected) - expected_size);
        memcpy(expected + expected_size, argv[i], size);
        expected_size += size;
    }
    check(AT_FDCWD, "/proc/self/cmdline", flags, 0);
    check(-1, "/proc/self/cmdline", flags, 0);
    check(proc, "self/cmdline", flags, 0);
    check(proc, "./self/cmdline", flags, 0);
    check(self, "cmdline", flags, 0);
    check(self, "cmdline", flags | O_NOFOLLOW, 0);
    check(self, "cmdline", O_PATH | O_CLOEXEC, 0);
    check(self, "cmdline", flags | O_DIRECTORY, ENOTDIR);
    assert(chdir("/proc") == 0);
    check(AT_FDCWD, "self/cmdline", flags, 0);
    check(-1, "self/cmdline", flags, EBADF);
    assert(fchdir(cwd) == 0);
    assert(close(self) == 0);
    assert(close(proc) == 0);
    assert(close(cwd) == 0);
    assert(getrlimit(RLIMIT_NOFILE, &saved) == 0);
    int next = open("/dev/null", O_RDONLY | O_CLOEXEC);

    assert(next >= 0);
    limit = saved;
    limit.rlim_cur = next + 1;
    assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
    assert(close(next) == 0);
    check(AT_FDCWD, "/proc/self/cmdline", flags, 0);
    check(AT_FDCWD, "/proc/self/cmdline", flags | O_NOFOLLOW, 0);
    assert(setrlimit(RLIMIT_NOFILE, &saved) == 0);
    return EXIT_SUCCESS;
}
