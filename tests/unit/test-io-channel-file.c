/*
 * QEMU I/O channel file test
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/channel-file.h"
#include "io/channel-util.h"
#include "io-channel-helpers.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TEST_FILE "tests/test-io-channel-file.txt"
#define TEST_MASK 0600

/*
 * On Windows the stat() function in the C library checks only
 * the FAT-style READONLY attribute and does not look at the ACL at all.
 */
#ifdef _WIN32
#define TEST_MASK_EXPECT 0700
#else
#define TEST_MASK_EXPECT 0777
#endif

static void test_io_channel_file_helper(int flags)
{
    QIOChannel *src, *dst;
    QIOChannelTest *test;
    struct stat st;
    mode_t mask;
    int ret;

    unlink(TEST_FILE);
    src = QIO_CHANNEL(qio_channel_file_new_path(
                          TEST_FILE,
                          flags, TEST_MASK,
                          &error_abort));
    dst = QIO_CHANNEL(qio_channel_file_new_path(
                          TEST_FILE,
                          O_RDONLY | O_BINARY, 0,
                          &error_abort));

    test = qio_channel_test_new();
    qio_channel_test_run_writer(test, src);
    qio_channel_test_run_reader(test, dst);
    qio_channel_test_validate(test);

    /* Check that the requested mode took effect. */
    mask = umask(0);
    umask(mask);
    ret = stat(TEST_FILE, &st);
    g_assert_cmpint(ret, >, -1);
    g_assert_cmpuint(TEST_MASK & ~mask, ==, st.st_mode & TEST_MASK_EXPECT);

    unlink(TEST_FILE);
    object_unref(OBJECT(src));
    object_unref(OBJECT(dst));
}

static void test_io_channel_file(void)
{
    test_io_channel_file_helper(O_WRONLY | O_CREAT | O_TRUNC | O_BINARY);
}

static void test_io_channel_file_rdwr(void)
{
    test_io_channel_file_helper(O_RDWR | O_CREAT | O_TRUNC | O_BINARY);
}

static void test_io_channel_fd(void)
{
    QIOChannel *ioc;
    int fd = -1;

    fd = open(TEST_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    g_assert_cmpint(fd, >, -1);

    ioc = qio_channel_new_fd(fd, &error_abort);

    g_assert_cmpstr(object_get_typename(OBJECT(ioc)),
                    ==,
                    TYPE_QIO_CHANNEL_FILE);

    unlink(TEST_FILE);
    object_unref(OBJECT(ioc));
}


#ifdef CONFIG_PREADV
#define TEST_PREAD_FILE "tests/test-io-channel-pread.txt"
#define TEST_PREAD_PATTERN "ABCDEFGHIJKLMNOP"  /* 16 bytes */
#define TEST_PREAD_LEN 16

static QIOChannel *create_pread_test_file(void)
{
    int fd;

    fd = open(TEST_PREAD_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, TEST_PREAD_PATTERN, TEST_PREAD_LEN),
                    ==, TEST_PREAD_LEN);
    close(fd);

    return QIO_CHANNEL(qio_channel_file_new_path(
                           TEST_PREAD_FILE, O_RDONLY, 0,
                           &error_abort));
}

static void test_io_channel_preadv_all(void)
{
    QIOChannel *ioc;
    char buf1[4], buf2[4];
    struct iovec iov[2] = {
        { .iov_base = buf1, .iov_len = sizeof(buf1) },
        { .iov_base = buf2, .iov_len = sizeof(buf2) },
    };
    int ret;

    ioc = create_pread_test_file();

    /* Read 8 bytes from offset 4 into two iovecs: "EFGH" + "IJKL" */
    ret = qio_channel_preadv_all(ioc, iov, 2, 4, &error_abort);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpmem(buf1, 4, "EFGH", 4);
    g_assert_cmpmem(buf2, 4, "IJKL", 4);

    unlink(TEST_PREAD_FILE);
    object_unref(OBJECT(ioc));
}

static void test_io_channel_pread_all(void)
{
    QIOChannel *ioc;
    char buf[8];
    int ret;

    ioc = create_pread_test_file();

    /* Read 8 bytes from offset 8: "IJKLMNOP" */
    ret = qio_channel_pread_all(ioc, buf, sizeof(buf), 8, &error_abort);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpmem(buf, 8, "IJKLMNOP", 8);

    unlink(TEST_PREAD_FILE);
    object_unref(OBJECT(ioc));
}

static void test_io_channel_preadv_all_eof_clean(void)
{
    QIOChannel *ioc;
    Error *err = NULL;
    char buf[8];
    struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
    int ret;

    ioc = create_pread_test_file();

    /* Read from offset == file length: clean EOF, expect 0 and no error */
    ret = qio_channel_preadv_all_eof(ioc, &iov, 1, TEST_PREAD_LEN, &err);
    g_assert_cmpint(ret, ==, 0);
    g_assert_null(err);

    unlink(TEST_PREAD_FILE);
    object_unref(OBJECT(ioc));
}

static void test_io_channel_preadv_all_eof_partial(void)
{
    QIOChannel *ioc;
    Error *err = NULL;
    char buf[8];
    struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
    int ret;

    ioc = create_pread_test_file();

    /*
     * Read 8 bytes from offset 12: only 4 bytes available before EOF.
     * Expect -1 (partial data then EOF is an error) and err set.
     */
    ret = qio_channel_preadv_all_eof(ioc, &iov, 1, 12, &err);
    g_assert_cmpint(ret, ==, -1);
    g_assert_nonnull(err);
    error_free(err);

    unlink(TEST_PREAD_FILE);
    object_unref(OBJECT(ioc));
}

static void test_io_channel_preadv_all_eof_is_error(void)
{
    QIOChannel *ioc;
    Error *err = NULL;
    char buf[8];
    struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
    int ret;

    ioc = create_pread_test_file();

    /*
     * Clean EOF through the strict wrapper: should be translated to -1.
     */
    ret = qio_channel_preadv_all(ioc, &iov, 1, TEST_PREAD_LEN, &err);
    g_assert_cmpint(ret, ==, -1);
    g_assert_nonnull(err);
    error_free(err);

    unlink(TEST_PREAD_FILE);
    object_unref(OBJECT(ioc));
}
#endif /* CONFIG_PREADV */


#ifndef _WIN32
static void test_io_channel_pipe(bool async)
{
    QIOChannel *src, *dst;
    QIOChannelTest *test;
    int fd[2];

    if (!g_unix_open_pipe(fd, FD_CLOEXEC, NULL)) {
        perror("pipe");
        abort();
    }

    src = QIO_CHANNEL(qio_channel_file_new_fd(fd[1]));
    dst = QIO_CHANNEL(qio_channel_file_new_fd(fd[0]));

    test = qio_channel_test_new();
    qio_channel_test_run_threads(test, async, src, dst);
    qio_channel_test_validate(test);

    object_unref(OBJECT(src));
    object_unref(OBJECT(dst));
}


static void test_io_channel_pipe_async(void)
{
    test_io_channel_pipe(true);
}

static void test_io_channel_pipe_sync(void)
{
    test_io_channel_pipe(false);
}
#endif /* ! _WIN32 */


int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/io/channel/file", test_io_channel_file);
    g_test_add_func("/io/channel/file/rdwr", test_io_channel_file_rdwr);
    g_test_add_func("/io/channel/file/fd", test_io_channel_fd);
#ifdef CONFIG_PREADV
    g_test_add_func("/io/channel/file/preadv-all",
                    test_io_channel_preadv_all);
    g_test_add_func("/io/channel/file/pread-all",
                    test_io_channel_pread_all);
    g_test_add_func("/io/channel/file/preadv-all-eof/clean",
                    test_io_channel_preadv_all_eof_clean);
    g_test_add_func("/io/channel/file/preadv-all-eof/partial",
                    test_io_channel_preadv_all_eof_partial);
    g_test_add_func("/io/channel/file/preadv-all/eof-is-error",
                    test_io_channel_preadv_all_eof_is_error);
#endif
#ifndef _WIN32
    g_test_add_func("/io/channel/pipe/sync", test_io_channel_pipe_sync);
    g_test_add_func("/io/channel/pipe/async", test_io_channel_pipe_async);
#endif
    return g_test_run();
}
