/*
 * QTest testcase for Dirty Page Rate Limit
 *
 * Copyright (c) 2022 CHINA TELECOM CO.,LTD.
 *
 * Authors:
 *  Hyman Huang(黄勇) <huangy81@chinatelecom.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

#include "migration-helpers.h"
#include "tests/migration/i386/a-b-bootblock.h"

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */

static const char *tmpfs;

static QDict *qmp_command(QTestState *who, const char *command, ...)
{
    va_list ap;
    QDict *resp, *ret;

    va_start(ap, command);
    resp = qtest_vqmp(who, command, ap);
    va_end(ap);

    g_assert(!qdict_haskey(resp, "error"));
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qdict(resp, "return");
    qobject_ref(ret);
    qobject_unref(resp);

    return ret;
}

static void calc_dirty_rate(QTestState *who, uint64_t calc_time)
{
    qobject_unref(qmp_command(who,
                  "{ 'execute': 'calc-dirty-rate',"
                  "'arguments': { "
                  "'calc-time': %ld,"
                  "'mode': 'dirty-ring' }}",
                  calc_time));
}

static QDict *query_dirty_rate(QTestState *who)
{
    return qmp_command(who, "{ 'execute': 'query-dirty-rate' }");
}

static void dirtylimit_set_all(QTestState *who, uint64_t dirtyrate)
{
    qobject_unref(qmp_command(who,
                  "{ 'execute': 'set-vcpu-dirty-limit',"
                  "'arguments': { "
                  "'dirty-rate': %ld } }",
                  dirtyrate));
}

static void cancel_vcpu_dirty_limit(QTestState *who)
{
    qobject_unref(qmp_command(who,
                  "{ 'execute': 'cancel-vcpu-dirty-limit' }"));
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
                                             int64_t calc_time)
{
    int max_try_count = 200;
    usleep(calc_time);

    while (!calc_dirtyrate_ready(who) && max_try_count--) {
        usleep(1000);
    }

    /*
     * Set the timeout with 200 ms(max_try_count * 1000us),
     * if dirtyrate measurement not complete, test failed.
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

static QTestState *start_vm(void)
{
    QTestState *vm = NULL;
    g_autofree gchar *cmd = NULL;
    const char *arch = qtest_get_arch();
    g_autofree char *bootpath = NULL;

    assert((strcmp(arch, "x86_64") == 0));
    bootpath = g_strdup_printf("%s/bootsect", tmpfs);
    assert(sizeof(x86_bootsect) == 512);
    init_bootfile(bootpath, x86_bootsect, sizeof(x86_bootsect));

    cmd = g_strdup_printf("-accel kvm,dirty-ring-size=4096 "
                          "-name dirtylimit-test,debug-threads=on "
                          "-m 150M -smp 1 "
                          "-serial file:%s/vm_serial "
                          "-drive file=%s,format=raw ",
                          tmpfs, bootpath);

    vm = qtest_init(cmd);
    return vm;
}

static void cleanup(const char *filename)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, filename);
    unlink(path);
}

static void stop_vm(QTestState *vm)
{
    qtest_quit(vm);
    cleanup("bootsect");
    cleanup("vm_serial");
}

static void test_vcpu_dirty_limit(void)
{
    QTestState *vm;
    int64_t origin_rate;
    int64_t quota_rate;
    int64_t rate ;
    int max_try_count = 5;
    int hit = 0;

    vm = start_vm();
    if (!vm) {
        return;
    }

    /* Wait for the first serial output from the vm*/
    wait_for_serial(tmpfs, "vm_serial");

    /* Do dirtyrate measurement with calc time equals 1s */
    calc_dirty_rate(vm, 1);

    /* Sleep a calc time and wait for calc dirtyrate complete */
    wait_for_calc_dirtyrate_complete(vm, 1 * 1000000);

    /* Query original dirty page rate */
    origin_rate = get_dirty_rate(vm);

    /* VM booted from bootsect should dirty memory */
    assert(origin_rate != 0);

    /* Setup quota dirty page rate at one-third of origin */
    quota_rate = origin_rate / 3;

    /* Set dirtylimit and wait a bit to check if it take effect */
    dirtylimit_set_all(vm, quota_rate);
    usleep(2000000);

    /*
     * Check if set-vcpu-dirty-limit and query-vcpu-dirty-limit
     * works literally
     */
    g_assert_cmpint(quota_rate, ==, get_limit_rate(vm));

    /* Check if dirtylimit take effect realistically */
    while (--max_try_count) {
        calc_dirty_rate(vm, 1);
        wait_for_calc_dirtyrate_complete(vm, 1 * 1000000);
        rate = get_dirty_rate(vm);

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
    max_try_count = 5;

    /* Check if dirtylimit cancellation take effect */
    cancel_vcpu_dirty_limit(vm);
    while (--max_try_count) {
        calc_dirty_rate(vm, 1);
        wait_for_calc_dirtyrate_complete(vm, 1 * 1000000);
        rate = get_dirty_rate(vm);

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
    stop_vm(vm);
}

int main(int argc, char **argv)
{
    char template[] = "/tmp/dirtylimit-test-XXXXXX";
    int ret;

    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path (%s): %s", template, strerror(errno));
    }
    g_assert(tmpfs);

    if (!kvm_dirty_ring_supported()) {
        return 0;
    }

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/dirtylimit/test", test_vcpu_dirty_limit);
    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }

    return ret;
}
