/*
 * Image lock tests
 *
 * Copyright 2016 Red Hat, Inc.
 *
 * Authors:
 *  Fam Zheng <famz@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "sysemu/block-backend.h"

#define DEBUG_IMAGE_LOCK_TEST 0
#define DPRINTF(...) do { \
        if (DEBUG_IMAGE_LOCK_TEST) { \
            printf(__VA_ARGS__); \
        } \
    } while (0)

#define TEST_IMAGE_SIZE 4096
static char test_image[] = "/tmp/qtest.XXXXXX";
static int test_image_fd;

static BlockBackend *open_test_image(const char *format,
                                     int flags, bool disable_lock, Error **errp)
{
    QDict *opts = qdict_new();

    qdict_set_default_str(opts, "driver", format);
    qdict_set_default_str(opts, "file.driver", "file");
    qdict_set_default_str(opts, "file.filename", test_image);
    qdict_set_default_str(opts, "file.locking", disable_lock ? "off" : "on");

    return blk_new_open(NULL, NULL, opts, flags, errp);
}

#define RW true
#define RO false
#define SHARE true
#define EXCLU false

static struct CompatData {
    bool write_1;
    bool share_1;
    bool write_2;
    bool share_2;
    bool compatible;
} compat_data[] = {
    /* Write 1, Share 1, Write 2, Share 2, Compatible. */
    { RO,       SHARE,   RO,      SHARE,   true,  },
    { RO,       SHARE,   RO,      EXCLU,   true,  },
    { RO,       SHARE,   RW,      SHARE,   true,  },
    { RO,       SHARE,   RW,      EXCLU,   true,  },

    { RO,       EXCLU,   RO,      EXCLU,   true,  },
    { RO,       EXCLU,   RW,      SHARE,   false, },
    { RO,       EXCLU,   RW,      EXCLU,   false, },

    { RW,       SHARE,   RW,      SHARE,   false, },
    { RW,       SHARE,   RW,      EXCLU,   false, },

    { RW,       EXCLU,   RW,      EXCLU,   false, },
};

static void dprint_flags(int flags)
{
    DPRINTF(" %8x", flags);
    if (flags & BDRV_O_RDWR) {
        DPRINTF(" RW");
    } else {
        DPRINTF(" RO");
    }
    if (flags & BDRV_O_UNSAFE_READ) {
        DPRINTF(" SHARE");
    } else {
        DPRINTF(" EXCLU");
    }
    DPRINTF("\n");
}

/* Test one combination scenario.
 *
 * @flags1: The flags of the first blk.
 * @flags2: The flags of the second blk.
 * @disable1: The value for raw-posix disable-lock option of the first blk.
 * @disable2: The value for raw-posix disable-lock option of the second blk.
 * @from_reopen: Whether or not the first blk should get flags1 from a reopen.
 * @initial: The source flags from which the blk1 is reopened, only
 *                effective if @from_reopen is true.
 */
static void do_test_compat_one(const char *format,
                               int flags1, int flags2,
                               bool disable1, bool disable2,
                               bool from_reopen, int initial_readonly,
                               bool compatible)
{
    int ret;
    BlockBackend *blk1, *blk2;
    int initial_flags = flags1;
    Error *local_err = NULL;

    DPRINTF("\n===\ndo test compat one\n");
    DPRINTF("flags1:");
    dprint_flags(flags1);
    DPRINTF("flags2:");
    dprint_flags(flags2);
    DPRINTF("disable %d %d\n", disable1, disable2);
    DPRINTF("from reopen %d, initial readonly: %d\n", from_reopen, initial_readonly);
    DPRINTF("compatible %d\n", compatible);
    if (!from_reopen) {
        blk1 = open_test_image(format, flags1, disable1, &local_err);
        if (local_err) {
            abort();
        }
    } else {
        if (initial_readonly) {
            initial_flags &= ~BDRV_O_RDWR;
        } else {
            initial_flags |= BDRV_O_RDWR;
        }
        if ((initial_flags & BDRV_O_RDWR) != (flags1 & BDRV_O_RDWR)) {
            flags1 |= BDRV_O_ALLOW_RDWR;
            initial_flags |= BDRV_O_ALLOW_RDWR;
        }
        DPRINTF("initial flags: ");
        dprint_flags(initial_flags);
        blk1 = open_test_image(format, initial_flags, disable1, &error_abort);
        BlockReopenQueue *rq = NULL;

        rq = bdrv_reopen_queue(rq, blk_bs(blk1), NULL, flags1);
        ret = bdrv_reopen_multiple(blk_get_aio_context(blk1), rq, &error_abort);
        g_assert_cmpint(ret, ==, 0);
    }
    g_assert_nonnull(blk1);
    blk2 = open_test_image(format, flags2, disable2, &local_err);
    if (compatible) {
        g_assert_nonnull(blk2);
    } else {
        g_assert_null(blk2);
    }
    blk_unref(blk2);
    if (from_reopen && initial_flags != flags1) {
        BlockReopenQueue *rq = NULL;

        rq = bdrv_reopen_queue(rq, blk_bs(blk1), NULL, initial_flags);
        ret = bdrv_reopen_multiple(blk_get_aio_context(blk1), rq, &error_abort);
        g_assert_cmpint(ret, ==, 0);
    }
    blk_unref(blk1);
}

static void do_test_compat(const char *format,
                           bool test_disable, bool from_reopen,
                           int initial_readonly)
{
    int i;
    int flags1, flags2;

    for (i = 0; i < ARRAY_SIZE(compat_data); i++) {
        struct CompatData *data = &compat_data[i];
        bool compat = data->compatible;

        flags1 = (data->write_1 ? BDRV_O_RDWR : 0) |
                 (data->share_1 ? BDRV_O_UNSAFE_READ : 0);
        flags2 = (data->write_2 ? BDRV_O_RDWR : 0) |
                 (data->share_2 ? BDRV_O_UNSAFE_READ : 0);
        if (!test_disable) {
            do_test_compat_one(format,
                               flags1, flags2, false, false,
                               from_reopen, initial_readonly, compat);

            do_test_compat_one(format,
                               flags2, flags1, false, false,
                               from_reopen, initial_readonly, compat);
        } else {
            compat = true;
            do_test_compat_one(format,
                               flags1, flags2, true, false,
                               from_reopen, initial_readonly, compat);
            do_test_compat_one(format,
                               flags1, flags2, false, true,
                               from_reopen, initial_readonly, compat);
            do_test_compat_one(format,
                               flags2, flags1, true, false,
                               from_reopen, initial_readonly, compat);
            do_test_compat_one(format,
                               flags2, flags1, false, true,
                               from_reopen, initial_readonly, compat);
            do_test_compat_one(format,
                               flags1, flags2, true, true,
                               from_reopen, initial_readonly, compat);
        }
    }
}

static void test_compat(void)
{
    bdrv_img_create(test_image, "qcow2", NULL, NULL, NULL, TEST_IMAGE_SIZE,
                    BDRV_O_RDWR, &error_fatal, true);
    do_test_compat("qcow2", false, false, false);
}

static void test_compat_after_reopen(void)
{
    bdrv_img_create(test_image, "qcow2", NULL, NULL, NULL, TEST_IMAGE_SIZE,
                    BDRV_O_RDWR, &error_fatal, true);
    do_test_compat("qcow2", false, true, false);
    do_test_compat("qcow2", false, true, true);
}

static void test_disable(void)
{
    bdrv_img_create(test_image, "qcow2", NULL, NULL, NULL, TEST_IMAGE_SIZE,
                    BDRV_O_RDWR, &error_fatal, true);
    do_test_compat("qcow2", true, false, 0);
    do_test_compat("qcow2", true, true, false);
    do_test_compat("qcow2", true, true, true);
}

static void test_0bytefile(void)
{
    int ret;

    ret = ftruncate(test_image_fd, 0);
    g_assert_cmpint(ret, ==, 0);
    do_test_compat("raw", false, false, 0);
}

int main(int argc, char **argv)
{
#ifndef F_OFD_SETLK
    return 0;
#endif
    int r;
    test_image_fd = mkstemp(test_image);
    g_assert(test_image_fd >= 0);

    qemu_init_main_loop(&error_fatal);
    bdrv_init();

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image-lock/compat", test_compat);
    g_test_add_func("/image-lock/compat_after_reopen", test_compat_after_reopen);
    g_test_add_func("/image-lock/disable", test_disable);
    g_test_add_func("/image-lock/compat_0bytefile", test_0bytefile);
    aio_context_acquire(qemu_get_aio_context());
    r = g_test_run();
    aio_context_release(qemu_get_aio_context());
    close(test_image_fd);
    unlink(test_image);
    return r;
}
