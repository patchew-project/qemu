/*
 * logging unit-tests
 *
 * Copyright (C) 2016 Linaro Ltd.
 *
 *  Author: Alex Bennée <alex.bennee@linaro.org>
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

#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/log.h"

static void test_parse_range(void)
{
    Error *err = NULL;

    qemu_set_dfilter_ranges("0x1000+0x100", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0xfff));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert(qemu_log_in_addr_range(0x1001));
    g_assert(qemu_log_in_addr_range(0x10ff));
    g_assert_false(qemu_log_in_addr_range(0x1100));

    qemu_set_dfilter_ranges("0x1000-0x100", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0x1001));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert(qemu_log_in_addr_range(0x0f01));
    g_assert_false(qemu_log_in_addr_range(0x0f00));

    qemu_set_dfilter_ranges("0x1000..0x1100", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0xfff));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert(qemu_log_in_addr_range(0x1100));
    g_assert_false(qemu_log_in_addr_range(0x1101));

    qemu_set_dfilter_ranges("0x1000..0x1000", &error_abort);

    g_assert_false(qemu_log_in_addr_range(0xfff));
    g_assert(qemu_log_in_addr_range(0x1000));
    g_assert_false(qemu_log_in_addr_range(0x1001));

    qemu_set_dfilter_ranges("0x1000+0x100,0x2100-0x100,0x3000..0x3100",
                            &error_abort);
    g_assert(qemu_log_in_addr_range(0x1050));
    g_assert(qemu_log_in_addr_range(0x2050));
    g_assert(qemu_log_in_addr_range(0x3050));

    qemu_set_dfilter_ranges("0xffffffffffffffff-1", &error_abort);
    g_assert(qemu_log_in_addr_range(UINT64_MAX));
    g_assert_false(qemu_log_in_addr_range(UINT64_MAX - 1));

    qemu_set_dfilter_ranges("0..0xffffffffffffffff", &err);
    g_assert(qemu_log_in_addr_range(0));
    g_assert(qemu_log_in_addr_range(UINT64_MAX));
 
    qemu_set_dfilter_ranges("2..1", &err);
    error_free_or_abort(&err);

    qemu_set_dfilter_ranges("0x1000+onehundred", &err);
    error_free_or_abort(&err);

    qemu_set_dfilter_ranges("0x1000+0", &err);
    error_free_or_abort(&err);
}

static void test_parse_path(gconstpointer data)
{
    gchar const *tmp_path = data;
    gchar *plain_path = g_build_filename(tmp_path, "qemu.log", NULL);
    gchar *pid_infix_path = g_build_filename(tmp_path, "qemu-%d.log", NULL);
    gchar *pid_suffix_path = g_build_filename(tmp_path, "qemu.log.%d", NULL);
    gchar *double_pid_path = g_build_filename(tmp_path, "qemu-%d%d.log", NULL);
    Error *err = NULL;

    qemu_set_log_filename(plain_path, &error_abort);
    qemu_set_log_filename(pid_infix_path, &error_abort);
    qemu_set_log_filename(pid_suffix_path, &error_abort);

    qemu_set_log_filename(double_pid_path, &err);
    error_free_or_abort(&err);

    g_free(double_pid_path);
    g_free(pid_suffix_path);
    g_free(pid_infix_path);
    g_free(plain_path);
}

static void rmtree(gchar const *root)
{
    /* There should really be a g_rmtree(). Implementing it ourselves
     * isn't really worth it just for a test, so let's just use rm. */
    gchar const *rm_args[] = { "rm", "-rf", root, NULL };
    g_spawn_sync(NULL, (gchar **)rm_args, NULL,
                 G_SPAWN_SEARCH_PATH, NULL, NULL,
                 NULL, NULL, NULL, NULL);
}

int main(int argc, char **argv)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-logging.XXXXXX", NULL);
    int rc;

    g_test_init(&argc, &argv, NULL);
    g_assert_nonnull(tmp_path);

    g_test_add_func("/logging/parse_range", test_parse_range);
    g_test_add_data_func("/logging/parse_path", tmp_path, test_parse_path);

    rc = g_test_run();

    rmtree(tmp_path);
    g_free(tmp_path);
    return rc;
}
