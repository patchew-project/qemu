/*
 * Test block device write threshold
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/write-threshold.h"


/*
 * bdrv_write_threshold_is_set
 *
 * Tell if a write threshold is set for a given BDS.
 */
static bool bdrv_write_threshold_is_set(const BlockDriverState *bs)
{
    return bs->write_threshold_offset > 0;
}

/*
 * bdrv_write_threshold_exceeded
 *
 * Return the extent of a write request that exceeded the threshold,
 * or zero if the request is below the threshold.
 * Return zero also if the threshold was not set.
 *
 * NOTE: here we assume the following holds for each request this code
 * deals with:
 *
 * assert((req->offset + req->bytes) <= UINT64_MAX)
 *
 * Please not there is *not* an actual C assert().
 */
static uint64_t bdrv_write_threshold_exceeded(const BlockDriverState *bs,
                                              const BdrvTrackedRequest *req)
{
    if (bdrv_write_threshold_is_set(bs)) {
        if (req->offset > bs->write_threshold_offset) {
            return (req->offset - bs->write_threshold_offset) + req->bytes;
        }
        if ((req->offset + req->bytes) > bs->write_threshold_offset) {
            return (req->offset + req->bytes) - bs->write_threshold_offset;
        }
    }
    return 0;
}

static void test_threshold_not_set_on_init(void)
{
    uint64_t res;
    BlockDriverState bs;
    memset(&bs, 0, sizeof(bs));

    g_assert(!bdrv_write_threshold_is_set(&bs));

    res = bdrv_write_threshold_get(&bs);
    g_assert_cmpint(res, ==, 0);
}

static void test_threshold_set_get(void)
{
    uint64_t threshold = 4 * 1024 * 1024;
    uint64_t res;
    BlockDriverState bs;
    memset(&bs, 0, sizeof(bs));

    bdrv_write_threshold_set(&bs, threshold);

    g_assert(bdrv_write_threshold_is_set(&bs));

    res = bdrv_write_threshold_get(&bs);
    g_assert_cmpint(res, ==, threshold);
}

static void test_threshold_multi_set_get(void)
{
    uint64_t threshold1 = 4 * 1024 * 1024;
    uint64_t threshold2 = 15 * 1024 * 1024;
    uint64_t res;
    BlockDriverState bs;
    memset(&bs, 0, sizeof(bs));

    bdrv_write_threshold_set(&bs, threshold1);
    bdrv_write_threshold_set(&bs, threshold2);
    res = bdrv_write_threshold_get(&bs);
    g_assert_cmpint(res, ==, threshold2);
}

static void test_threshold_not_trigger(void)
{
    uint64_t amount = 0;
    uint64_t threshold = 4 * 1024 * 1024;
    BlockDriverState bs;
    BdrvTrackedRequest req;

    memset(&bs, 0, sizeof(bs));
    memset(&req, 0, sizeof(req));
    req.offset = 1024;
    req.bytes = 1024;

    bdrv_check_request(req.offset, req.bytes, &error_abort);

    bdrv_write_threshold_set(&bs, threshold);
    amount = bdrv_write_threshold_exceeded(&bs, &req);
    g_assert_cmpuint(amount, ==, 0);
}


static void test_threshold_trigger(void)
{
    uint64_t amount = 0;
    uint64_t threshold = 4 * 1024 * 1024;
    BlockDriverState bs;
    BdrvTrackedRequest req;

    memset(&bs, 0, sizeof(bs));
    memset(&req, 0, sizeof(req));
    req.offset = (4 * 1024 * 1024) - 1024;
    req.bytes = 2 * 1024;

    bdrv_check_request(req.offset, req.bytes, &error_abort);

    bdrv_write_threshold_set(&bs, threshold);
    amount = bdrv_write_threshold_exceeded(&bs, &req);
    g_assert_cmpuint(amount, >=, 1024);
}

typedef struct TestStruct {
    const char *name;
    void (*func)(void);
} TestStruct;


int main(int argc, char **argv)
{
    size_t i;
    TestStruct tests[] = {
        { "/write-threshold/not-set-on-init",
          test_threshold_not_set_on_init },
        { "/write-threshold/set-get",
          test_threshold_set_get },
        { "/write-threshold/multi-set-get",
          test_threshold_multi_set_get },
        { "/write-threshold/not-trigger",
          test_threshold_not_trigger },
        { "/write-threshold/trigger",
          test_threshold_trigger },
        { NULL, NULL }
    };

    g_test_init(&argc, &argv, NULL);
    for (i = 0; tests[i].name != NULL; i++) {
        g_test_add_func(tests[i].name, tests[i].func);
    }
    return g_test_run();
}
