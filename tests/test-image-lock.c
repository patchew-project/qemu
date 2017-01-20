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

static BlockBackend *open_test_image(int flags, bool disable_lock)
{
    QDict *opts = qdict_new();

    qdict_set_default_str(opts, "filename", test_image);
    qdict_set_default_str(opts, "driver", "file");
    if (disable_lock) {
        qdict_put(opts, "disable-lock", qbool_from_bool(true));
    }

    return blk_new_open(NULL, NULL, opts, flags | BDRV_O_ALLOW_RDWR, NULL);
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

    { RW,       SHARE,   RW,      SHARE,   true, },
    { RW,       SHARE,   RW,      EXCLU,   false, },

    { RW,       EXCLU,   RW,      EXCLU,   false, },
};

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
static void do_test_compat_one(int flags1, int flags2,
                               bool disable1, bool disable2,
                               bool from_reopen, int initial_flags,
                               bool compatible)
{
    BlockBackend *blk1, *blk2;

    DPRINTF("\n===\ndo test compat one\n");
    DPRINTF("flags %x %x\n", flags1, flags2);
    DPRINTF("disable %d %d\n", disable1, disable2);
    DPRINTF("from reopen %d, initial flags %d\n", from_reopen, initial_flags);
    DPRINTF("compatible %d\n", compatible);
    if (!from_reopen) {
        blk1 = open_test_image(flags1, disable1);
    } else {
        int ret;
        blk1 = open_test_image(initial_flags, disable1);
        BlockReopenQueue *rq = NULL;

        rq = bdrv_reopen_queue(rq, blk_bs(blk1), NULL, flags1);
        ret = bdrv_reopen_multiple(blk_get_aio_context(blk1), rq, &error_abort);
        g_assert_cmpint(ret, ==, 0);
    }
    g_assert_nonnull(blk1);
    g_assert_cmphex(blk_get_flags(blk1) & (BDRV_O_SHARE_RW | BDRV_O_RDWR),
                    ==, flags1);
    blk2 = open_test_image(flags2, disable2);
    if (compatible) {
        g_assert_nonnull(blk2);
    } else {
        g_assert_null(blk2);
    }
    blk_unref(blk1);
    blk_unref(blk2);
}

static void do_test_compat(bool test_disable, bool from_reopen,
                           int initial_flags)
{
    int i;
    int flags1, flags2;

    for (i = 0; i < ARRAY_SIZE(compat_data); i++) {
        struct CompatData *data = &compat_data[i];
        bool compat = data->compatible;

        flags1 = (data->write_1 ? BDRV_O_RDWR : 0) |
                 (data->share_1 ? BDRV_O_SHARE_RW : 0);
        flags2 = (data->write_2 ? BDRV_O_RDWR : 0) |
                 (data->share_2 ? BDRV_O_SHARE_RW : 0);
        if (!test_disable) {
            do_test_compat_one(flags1, flags2, false, false,
                               from_reopen, initial_flags, compat);

            do_test_compat_one(flags2, flags1, false, false,
                               from_reopen, initial_flags, compat);
        } else {
            compat = true;
            do_test_compat_one(flags1, flags2, true, false,
                               from_reopen, initial_flags, compat);
            do_test_compat_one(flags1, flags2, false, true,
                               from_reopen, initial_flags, compat);
            do_test_compat_one(flags2, flags1, true, false,
                               from_reopen, initial_flags, compat);
            do_test_compat_one(flags2, flags1, false, true,
                               from_reopen, initial_flags, compat);
            do_test_compat_one(flags1, flags2, true, true,
                               from_reopen, initial_flags, compat);
        }
    }
}

static void test_compat(void)
{
    do_test_compat(false, false, 0);
}

static void test_compat_after_reopen(void)
{
    do_test_compat(false, true, 0);
    do_test_compat(false, true, BDRV_O_SHARE_RW);
    do_test_compat(false, true, BDRV_O_RDWR);
    do_test_compat(false, true, BDRV_O_RDWR | BDRV_O_SHARE_RW);
}

static void test_0bytefile(void)
{
    ftruncate(test_image_fd, 0);
    do_test_compat(false, false, 0);
}

static void test_disable(void)
{
    do_test_compat(true, false, 0);
    do_test_compat(true, true, 0);
    do_test_compat(true, true, BDRV_O_SHARE_RW);
    do_test_compat(true, true, BDRV_O_RDWR);
    do_test_compat(true, true, BDRV_O_RDWR | BDRV_O_SHARE_RW);
}

int main(int argc, char **argv)
{
    int r;
    test_image_fd = mkstemp(test_image);

    qemu_init_main_loop(&error_fatal);
    bdrv_init();

    g_assert(test_image_fd >= 0);
    ftruncate(test_image_fd, TEST_IMAGE_SIZE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image-lock/compat", test_compat);
    g_test_add_func("/image-lock/compat_after_reopen", test_compat_after_reopen);
    g_test_add_func("/image-lock/compat_0bytefile", test_0bytefile);
    g_test_add_func("/image-lock/disable", test_disable);
    aio_context_acquire(qemu_get_aio_context());
    r = g_test_run();
    aio_context_release(qemu_get_aio_context());
    return r;
}
