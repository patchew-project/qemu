/*
 * BlockBackend tests
 *
 * Copyright (c) 2017 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "block/block_int.h"
#include "system/block-backend.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"

static void test_drain_aio_error_flush_cb(void *opaque, int ret)
{
    bool *completed = opaque;

    g_assert(ret == -ENOMEDIUM);
    *completed = true;
}

static void test_drain_aio_error(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(),
                                BLK_PERM_ALL, BLK_PERM_ALL);
    BlockAIOCB *acb;
    bool completed = false;

    acb = blk_aio_flush(blk, test_drain_aio_error_flush_cb, &completed);
    g_assert(acb != NULL);
    g_assert(completed == false);

    blk_drain(blk);
    g_assert(completed == true);

    blk_unref(blk);
}

static void test_drain_all_aio_error(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(),
                                BLK_PERM_ALL, BLK_PERM_ALL);
    BlockAIOCB *acb;
    bool completed = false;

    acb = blk_aio_flush(blk, test_drain_aio_error_flush_cb, &completed);
    g_assert(acb != NULL);
    g_assert(completed == false);

    blk_drain_all();
    g_assert(completed == true);

    blk_unref(blk);
}

static int bdrv_test_co_change_backing_file(BlockDriverState *bs,
                                            const char *backing_file,
                                            const char *backing_fmt)
{
    return 0; /* just return success so bdrv_set_backing_hd() works */
}

static BlockDriver bdrv_test = {
    .format_name = "test",
    .supports_backing = true,
    .bdrv_child_perm  = bdrv_default_perms,
    .bdrv_co_change_backing_file = bdrv_test_co_change_backing_file,
};

typedef struct {
    Notifier attach_notifier;
    Notifier detach_notifier;
    GArray *notifications;
} AttachDetach;

typedef enum {
    NOTIFY_END = 0,
    NOTIFY_ATTACH,
    NOTIFY_DETACH,
} NotificationType;

typedef struct {
    NotificationType type;
    BlockDriverState *bs;
} Notification;

static void attach_detach_append(AttachDetach *ad, NotificationType type,
                                 BlockBackendAttachDetachArgs *args)
{
    Notification n = {
        .type = type,
        .bs = args->bs,
    };

    g_array_append_vals(ad->notifications, &n, 1);
}

static void attach_notify(Notifier *notifier, void *data)
{
    AttachDetach *ad = container_of(notifier, AttachDetach, attach_notifier);
    attach_detach_append(ad, NOTIFY_ATTACH, data);
}

static void detach_notify(Notifier *notifier, void *data)
{
    AttachDetach *ad = container_of(notifier, AttachDetach, attach_notifier);
    attach_detach_append(ad, NOTIFY_DETACH, data);
}

static void attach_detach_init(AttachDetach *ad, BlockBackend *blk)
{
    ad->attach_notifier.notify = attach_notify;
    ad->detach_notifier.notify = detach_notify;
    ad->notifications = g_array_new(true, true, sizeof(Notification));

    blk_add_attach_notifier(blk, &ad->attach_notifier);
    blk_add_detach_notifier(blk, &ad->detach_notifier);
}

static void attach_detach_cleanup(AttachDetach *ad)
{
    g_array_free(ad->notifications, true);
    notifier_remove(&ad->detach_notifier);
    notifier_remove(&ad->attach_notifier);
}

/*
 * Check that the expected notifications occurred. @expected is terminated by a
 * NOTIFY_END element.
 */
static void attach_detach_expect(AttachDetach *ad, const Notification *expected)
{
    GArray *array = ad->notifications;

    /* The array is zero terminated so there is at least one element */
    Notification *actual = (Notification *)array->data;

    while (expected->type != NOTIFY_END) {
        g_assert_cmpint(actual->type, ==, expected->type);
        g_assert(actual->bs == expected->bs);
        expected++;
        actual++;
    }

    g_assert_cmpint(actual->type, ==, NOTIFY_END);

    g_array_remove_range(array, 0, array->len);
}

static void test_attach_detach_notifier(void)
{
    AttachDetach ad;
    BlockDriverState *format;
    BlockDriverState *file;
    BlockDriverState *file2;
    BlockBackend *blk = blk_new(qemu_get_aio_context(),
                                BLK_PERM_ALL, BLK_PERM_ALL);

    attach_detach_init(&ad, blk);

    format = bdrv_new_open_driver(&bdrv_test, "format", BDRV_O_RDWR,
                                  &error_abort);
    file = bdrv_new_open_driver(&bdrv_test, "file", BDRV_O_RDWR, &error_abort);
    file2 = bdrv_new_open_driver(&bdrv_test, "file2", BDRV_O_RDWR,
                                 &error_abort);

    bdrv_graph_wrlock_drained();
    bdrv_attach_child(format, file, "file", &child_of_bds,
                      BDRV_CHILD_PRIMARY | BDRV_CHILD_DATA, &error_abort);
    bdrv_graph_wrunlock();

    /* Insert format -> file */
    blk_insert_bs(blk, format, &error_abort);
    attach_detach_expect(&ad, (Notification[]){
        (Notification){NOTIFY_ATTACH, format},
        {},
    });

    /* Replace format -> file with file2 */
    blk_replace_bs(blk, file2, &error_abort);
    attach_detach_expect(&ad, (Notification[]){
        (Notification){NOTIFY_DETACH, format},
        (Notification){NOTIFY_ATTACH, file2},
        {},
    });

    /* Remove file2 */
    blk_remove_bs(blk);
    attach_detach_expect(&ad, (Notification[]){
        (Notification){NOTIFY_DETACH, file2},
        {},
    });

    /* These BDSes were unrefed so we need new instances */
    file = bdrv_new_open_driver(&bdrv_test, "file", BDRV_O_RDWR, &error_abort);
    file2 = bdrv_new_open_driver(&bdrv_test, "file2", BDRV_O_RDWR,
                                 &error_abort);

    /* Replace a non-root node */
    bdrv_graph_wrlock_drained();
    bdrv_attach_child(format, file, "file", &child_of_bds,
                      BDRV_CHILD_PRIMARY | BDRV_CHILD_DATA, &error_abort);
    bdrv_replace_node(file, file2, &error_abort);
    bdrv_graph_wrunlock();
    attach_detach_expect(&ad, (Notification[]){
        (Notification){NOTIFY_ATTACH, file},
        (Notification){NOTIFY_DETACH, file},
        (Notification){NOTIFY_ATTACH, file2},
        {},
    });

    attach_detach_cleanup(&ad);
    blk_unref(blk);
    bdrv_unref(format);
}

int main(int argc, char **argv)
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/block-backend/drain_aio_error", test_drain_aio_error);
    g_test_add_func("/block-backend/drain_all_aio_error",
                    test_drain_all_aio_error);
    g_test_add_func("/block-backend/attach_detach_notifier",
                    test_attach_detach_notifier);

    return g_test_run();
}
