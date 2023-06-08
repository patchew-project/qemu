/*
 * QTest testcase for vcpu-dirty-limit
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "qapi/qmp/qlist.h"

#include "migration-helpers.h"

/* For dirty ring test */
#if defined(__linux__) && defined(HOST_X86_64)
#include "linux/kvm.h"
#include <sys/ioctl.h>
#endif

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */

static void calc_dirty_rate(QTestState *who, uint64_t calc_time)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'calc-dirty-rate',"
                             "'arguments': { "
                             "'calc-time': %" PRIu64 ","
                             "'mode': 'dirty-ring' }}",
                             calc_time);
}

static QDict *query_dirty_rate(QTestState *who)
{
    return qtest_qmp_assert_success_ref(who,
                                        "{ 'execute': 'query-dirty-rate' }");
}

static void dirtylimit_set_all(QTestState *who, uint64_t dirtyrate)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'set-vcpu-dirty-limit',"
                             "'arguments': { "
                             "'dirty-rate': %" PRIu64 " } }",
                             dirtyrate);
}

static void cancel_vcpu_dirty_limit(QTestState *who)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'cancel-vcpu-dirty-limit' }");
}

static QDict *query_vcpu_dirty_limit(QTestState *who)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'query-vcpu-dirty-limit' }");
    g_assert(!qdict_haskey(rsp, "error"));
    g_assert(qdict_haskey(rsp, "return"));

    return rsp;
}

static bool calc_dirtyrate_ready(QTestState *who)
{
    QDict *rsp_return;
    gchar *status;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = g_strdup(qdict_get_str(rsp_return, "status"));
    g_assert(status);

    return g_strcmp0(status, "measuring");
}

static void wait_for_calc_dirtyrate_complete(QTestState *who,
                                             int64_t time_s)
{
    int max_try_count = 10000;
    usleep(time_s * 1000000);

    while (!calc_dirtyrate_ready(who) && max_try_count--) {
        usleep(1000);
    }

    /*
     * Set the timeout with 10 s(max_try_count * 1000us),
     * if dirtyrate measurement not complete, fail test.
     */
    g_assert_cmpint(max_try_count, !=, 0);
}

static int64_t get_dirty_rate(QTestState *who)
{
    QDict *rsp_return;
    gchar *status;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = g_strdup(qdict_get_str(rsp_return, "status"));
    g_assert(status);
    g_assert_cmpstr(status, ==, "measured");

    rates = qdict_get_qlist(rsp_return, "vcpu-dirty-rate");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "dirty-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static int64_t get_limit_rate(QTestState *who)
{
    QDict *rsp_return;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_vcpu_dirty_limit(who);
    g_assert(rsp_return);

    rates = qdict_get_qlist(rsp_return, "return");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "limit-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static GuestState *dirtylimit_start_vm(void)
{
    GuestState *vm = guest_create("dirtylimit-test");

    guest_use_dirty_ring(vm);
    guest_realize(vm);

    return vm;
}

static void dirtylimit_stop_vm(GuestState *vm)
{
    guest_destroy(vm);
}

static void test_vcpu_dirty_limit(void)
{
    int64_t origin_rate;
    int64_t quota_rate;
    int64_t rate ;
    int max_try_count = 20;
    int hit = 0;

    /* Start vm for vcpu dirtylimit test */
    GuestState *vm = dirtylimit_start_vm();

    /* Wait for the first serial output from the vm*/
    wait_for_serial(vm);

    /* Do dirtyrate measurement with calc time equals 1s */
    calc_dirty_rate(vm->qs, 1);

    /* Sleep calc time and wait for calc dirtyrate complete */
    wait_for_calc_dirtyrate_complete(vm->qs, 1);

    /* Query original dirty page rate */
    origin_rate = get_dirty_rate(vm->qs);

    /* VM booted from bootsect should dirty memory steadily */
    assert(origin_rate != 0);

    /* Setup quota dirty page rate at half of origin */
    quota_rate = origin_rate / 2;

    /* Set dirtylimit */
    dirtylimit_set_all(vm->qs, quota_rate);

    /*
     * Check if set-vcpu-dirty-limit and query-vcpu-dirty-limit
     * works literally
     */
    g_assert_cmpint(quota_rate, ==, get_limit_rate(vm->qs));

    /* Sleep a bit to check if it take effect */
    usleep(2000000);

    /*
     * Check if dirtylimit take effect realistically, set the
     * timeout with 20 s(max_try_count * 1s), if dirtylimit
     * doesn't take effect, fail test.
     */
    while (--max_try_count) {
        calc_dirty_rate(vm->qs, 1);
        wait_for_calc_dirtyrate_complete(vm->qs, 1);
        rate = get_dirty_rate(vm->qs);

        /*
         * Assume hitting if current rate is less
         * than quota rate (within accepting error)
         */
        if (rate < (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);

    hit = 0;
    max_try_count = 20;

    /* Check if dirtylimit cancellation take effect */
    cancel_vcpu_dirty_limit(vm->qs);
    while (--max_try_count) {
        calc_dirty_rate(vm->qs, 1);
        wait_for_calc_dirtyrate_complete(vm->qs, 1);
        rate = get_dirty_rate(vm->qs);

        /*
         * Assume dirtylimit be canceled if current rate is
         * greater than quota rate (within accepting error)
         */
        if (rate > (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);
    dirtylimit_stop_vm(vm);
}

int main(int argc, char **argv)
{
    g_autoptr(GError) err = NULL;

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_accel("kvm")) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    if (!g_str_equal(qtest_get_arch(), "x86_64")) {
        g_test_skip("Only x86_64 support available");
        return 0;
    }

    if (!kvm_dirty_ring_supported()) {
        g_test_skip("KVM dirty ring is not supported");
        return 0;
    }

    tmpfs = g_dir_make_tmp("vcpu-dirty-limit-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
    }
    g_assert(tmpfs);
    bootfile_create(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    qtest_add_func("/vcpu_dirty_limit/basic", test_vcpu_dirty_limit);

    int ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    bootfile_delete();
    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }
    g_free(tmpfs);

    return ret;
}
