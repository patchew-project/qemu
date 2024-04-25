/*
 * Test DSA functions.
 *
 * Copyright (c) 2023 Hao Xiang <hao.xiang@bytedance.com>
 * Copyright (c) 2023 Bryan Zhang <bryan.zhang@bytedance.com>
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
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"

#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include "qemu/dsa.h"

/*
 * TODO Communicate that DSA must be configured to support this batch size.
 * TODO Alternatively, poke the DSA device to figure out batch size.
 */
#define batch_size 128
#define page_size 4096

#define oversized_batch_size (batch_size + 1)
#define num_devices 2
#define max_buffer_size (64 * 1024)

/* TODO Make these not-hardcoded. */
static const char *path1 = "/dev/dsa/wq4.0";
static const char *path2 = "/dev/dsa/wq4.0 /dev/dsa/wq4.1";

static struct batch_task *task;

/* A helper for running a single task and checking for correctness. */
static void do_single_task(void)
{
    task = batch_task_init(batch_size);
    char buf[page_size];
    char *ptr = buf;

    buffer_is_zero_dsa_batch_async(task,
                                   (const void **)&ptr,
                                   1,
                                   page_size);
    g_assert(task->results[0] == buffer_is_zero(buf, page_size));

    batch_task_destroy(task);
}

static void test_single_zero(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    task = batch_task_init(batch_size);

    char buf[page_size];
    char *ptr = buf;

    memset(buf, 0x0, page_size);
    buffer_is_zero_dsa_batch_async(task,
                                   (const void **)&ptr,
                                   1, page_size);
    g_assert(task->results[0]);

    batch_task_destroy(task);

    dsa_cleanup();
}

static void test_single_zero_async(void)
{
    test_single_zero();
}

static void test_single_nonzero(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    task = batch_task_init(batch_size);

    char buf[page_size];
    char *ptr = buf;

    memset(buf, 0x1, page_size);
    buffer_is_zero_dsa_batch_async(task,
                                   (const void **)&ptr,
                                   1, page_size);
    g_assert(!task->results[0]);

    batch_task_destroy(task);

    dsa_cleanup();
}

static void test_single_nonzero_async(void)
{
    test_single_nonzero();
}

/* count == 0 should return quickly without calling into DSA. */
static void test_zero_count_async(void)
{
    char buf[page_size];
    buffer_is_zero_dsa_batch_async(task,
                             (const void **)&buf,
                             0,
                             page_size);
}

static void test_null_task_async(void)
{
    if (g_test_subprocess()) {
        g_assert(!dsa_init(path1));

        char buf[page_size * batch_size];
        char *addrs[batch_size];
        for (int i = 0; i < batch_size; i++) {
            addrs[i] = buf + (page_size * i);
        }

        buffer_is_zero_dsa_batch_async(NULL, (const void **)addrs,
                                      batch_size,
                                      page_size);
    } else {
        g_test_trap_subprocess(NULL, 0, 0);
        g_test_trap_assert_failed();
    }
}

static void test_oversized_batch(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    task = batch_task_init(batch_size);

    char buf[page_size * oversized_batch_size];
    char *addrs[batch_size];
    for (int i = 0; i < oversized_batch_size; i++) {
        addrs[i] = buf + (page_size * i);
    }

    int ret = buffer_is_zero_dsa_batch_async(task,
                                            (const void **)addrs,
                                            oversized_batch_size,
                                            page_size);
    g_assert(ret != 0);

    batch_task_destroy(task);

    dsa_cleanup();
}

static void test_oversized_batch_async(void)
{
    test_oversized_batch();
}

static void test_zero_len_async(void)
{
    if (g_test_subprocess()) {
        g_assert(!dsa_init(path1));

        task = batch_task_init(batch_size);

        char buf[page_size];

        buffer_is_zero_dsa_batch_async(task,
                                       (const void **)&buf,
                                       1,
                                       0);

        batch_task_destroy(task);
    } else {
        g_test_trap_subprocess(NULL, 0, 0);
        g_test_trap_assert_failed();
    }
}

static void test_null_buf_async(void)
{
    if (g_test_subprocess()) {
        g_assert(!dsa_init(path1));

        task = batch_task_init(batch_size);

        buffer_is_zero_dsa_batch_async(task, NULL, 1, page_size);

        batch_task_destroy(task);
    } else {
        g_test_trap_subprocess(NULL, 0, 0);
        g_test_trap_assert_failed();
    }
}

static void test_batch(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    task = batch_task_init(batch_size);

    char buf[page_size * batch_size];
    char *addrs[batch_size];
    for (int i = 0; i < batch_size; i++) {
        addrs[i] = buf + (page_size * i);
    }

    /*
     * Using whatever is on the stack is somewhat random.
     * Manually set some pages to zero and some to nonzero.
     */
    memset(buf + 0, 0, page_size * 10);
    memset(buf + (10 * page_size), 0xff, page_size * 10);

    buffer_is_zero_dsa_batch_async(task,
                                   (const void **)addrs,
                                   batch_size,
                                   page_size);

    bool is_zero;
    for (int i = 0; i < batch_size; i++) {
        is_zero = buffer_is_zero((const void *)&buf[page_size * i], page_size);
        g_assert(task->results[i] == is_zero);
    }

    batch_task_destroy(task);

    dsa_cleanup();
}

static void test_batch_async(void)
{
    test_batch();
}

static void test_page_fault(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    char *buf[2];
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED | MAP_ANON;
    buf[0] = (char *)mmap(NULL, page_size * batch_size, prot, flags, -1, 0);
    assert(buf[0] != MAP_FAILED);
    buf[1] = (char *)malloc(page_size * batch_size);
    assert(buf[1] != NULL);

    for (int j = 0; j < 2; j++) {
        task = batch_task_init(batch_size);

        char *addrs[batch_size];
        for (int i = 0; i < batch_size; i++) {
            addrs[i] = buf[j] + (page_size * i);
        }

        buffer_is_zero_dsa_batch_async(task,
                                       (const void **)addrs,
                                       batch_size,
                                       page_size);

        bool is_zero;
        for (int i = 0; i < batch_size; i++) {
            is_zero = buffer_is_zero((const void *)&buf[j][page_size * i],
                                      page_size);
            g_assert(task->results[i] == is_zero);
        }
        batch_task_destroy(task);
    }

    assert(!munmap(buf[0], page_size * batch_size));
    free(buf[1]);
    dsa_cleanup();
}

static void test_various_buffer_sizes(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    char *buf = malloc(max_buffer_size * batch_size);
    char *addrs[batch_size];

    for (int len = 16; len <= max_buffer_size; len *= 2) {
        task = batch_task_init(batch_size);

        for (int i = 0; i < batch_size; i++) {
            addrs[i] = buf + (len * i);
        }

        buffer_is_zero_dsa_batch_async(task,
                                       (const void **)addrs,
                                       batch_size,
                                       len);

        bool is_zero;
        for (int j = 0; j < batch_size; j++) {
            is_zero = buffer_is_zero((const void *)&buf[len * j], len);
            g_assert(task->results[j] == is_zero);
        }

        batch_task_destroy(task);
    }

    free(buf);

    dsa_cleanup();
}

static void test_various_buffer_sizes_async(void)
{
    test_various_buffer_sizes();
}

static void test_double_start_stop(void)
{
    g_assert(!dsa_init(path1));
    /* Double start */
    dsa_start();
    dsa_start();
    g_assert(dsa_is_running());
    do_single_task();

    /* Double stop */
    dsa_stop();
    g_assert(!dsa_is_running());
    dsa_stop();
    g_assert(!dsa_is_running());

    /* Restart */
    dsa_start();
    g_assert(dsa_is_running());
    do_single_task();
    dsa_cleanup();
}

static void test_is_running(void)
{
    g_assert(!dsa_init(path1));

    g_assert(!dsa_is_running());
    dsa_start();
    g_assert(dsa_is_running());
    dsa_stop();
    g_assert(!dsa_is_running());
    dsa_cleanup();
}

static void test_multiple_engines(void)
{
    g_assert(!dsa_init(path2));
    dsa_start();

    struct batch_task *tasks[num_devices];
    char bufs[num_devices][page_size * batch_size];
    char *addrs[num_devices][batch_size];

    /*
     *  This is a somewhat implementation-specific way
     *  of testing that the tasks have unique engines
     *  assigned to them.
     */
    tasks[0] = batch_task_init(batch_size);
    tasks[1] = batch_task_init(batch_size);
    g_assert(tasks[0]->dsa_batch->device != tasks[1]->dsa_batch->device);

    for (int i = 0; i < num_devices; i++) {
        for (int j = 0; j < batch_size; j++) {
            addrs[i][j] = bufs[i] + (page_size * j);
        }

        buffer_is_zero_dsa_batch_async(tasks[i],
                                       (const void **)addrs[i],
                                       batch_size, page_size);

        bool is_zero;
        for (int j = 0; j < batch_size; j++) {
            is_zero = buffer_is_zero((const void *)&bufs[i][page_size * j],
                                     page_size);
            g_assert(tasks[i]->results[j] == is_zero);
        }
    }

    batch_task_destroy(tasks[0]);
    batch_task_destroy(tasks[1]);

    dsa_cleanup();
}

static void test_configure_dsa_twice(void)
{
    g_assert(!dsa_init(path2));
    g_assert(!dsa_init(path2));
    dsa_start();
    do_single_task();
    dsa_cleanup();
}

static void test_configure_dsa_bad_path(void)
{
    const char *bad_path = "/not/a/real/path";
    g_assert(dsa_init(bad_path));
}

static void test_cleanup_before_configure(void)
{
    dsa_cleanup();
    g_assert(!dsa_init(path2));
}

static void test_configure_dsa_num_devices(void)
{
    g_assert(!dsa_init(path1));
    dsa_start();

    do_single_task();
    dsa_stop();
    dsa_cleanup();
}

static void test_cleanup_twice(void)
{
    g_assert(!dsa_init(path2));
    dsa_cleanup();
    dsa_cleanup();

    g_assert(!dsa_init(path2));
    dsa_start();
    do_single_task();
    dsa_cleanup();
}

static int check_test_setup(void)
{
    const char *path[2] = {path1, path2};
    for (int i = 0; i < sizeof(path) / sizeof(char *); i++) {
        if (dsa_init(path[i])) {
            return -1;
        }
        dsa_cleanup();
    }
    return 0;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (check_test_setup() != 0) {
        /*
         * This test requires extra setup. The current
         * setup is not correct. Just skip this test
         * for now.
         */
        exit(0);
    }

    if (num_devices > 1) {
        g_test_add_func("/dsa/multiple_engines", test_multiple_engines);
    }

    g_test_add_func("/dsa/async/batch", test_batch_async);
    g_test_add_func("/dsa/async/various_buffer_sizes",
                    test_various_buffer_sizes_async);
    g_test_add_func("/dsa/async/null_buf", test_null_buf_async);
    g_test_add_func("/dsa/async/zero_len", test_zero_len_async);
    g_test_add_func("/dsa/async/oversized_batch", test_oversized_batch_async);
    g_test_add_func("/dsa/async/zero_count", test_zero_count_async);
    g_test_add_func("/dsa/async/single_zero", test_single_zero_async);
    g_test_add_func("/dsa/async/single_nonzero", test_single_nonzero_async);
    g_test_add_func("/dsa/async/null_task", test_null_task_async);
    g_test_add_func("/dsa/async/page_fault", test_page_fault);

    g_test_add_func("/dsa/double_start_stop", test_double_start_stop);
    g_test_add_func("/dsa/is_running", test_is_running);

    g_test_add_func("/dsa/configure_dsa_twice", test_configure_dsa_twice);
    g_test_add_func("/dsa/configure_dsa_bad_path", test_configure_dsa_bad_path);
    g_test_add_func("/dsa/cleanup_before_configure",
                    test_cleanup_before_configure);
    g_test_add_func("/dsa/configure_dsa_num_devices",
                    test_configure_dsa_num_devices);
    g_test_add_func("/dsa/cleanup_twice", test_cleanup_twice);

    return g_test_run();
}
