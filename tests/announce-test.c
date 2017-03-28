/*
 * QTest testcase for migration announce packets
 *
 * Copyright (c) 2017 Red Hat, Inc. and/or its affiliates
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"

static const char *tmpfs;

/*
 * Events can get in the way of responses we are actually waiting for.
 */
static QDict *return_or_event(QDict *response)
{
    if (!qdict_haskey(response, "event")) {
        return response;
    }

    /* OK, it was an event */
    QDECREF(response);
    return return_or_event(qtest_qmp_receive(global_qtest));
}


static void wait_for_migration_complete(void)
{
    QDict *rsp, *rsp_return;
    bool completed;

    do {
        const char *status;

        rsp = return_or_event(qmp("{ 'execute': 'query-migrate' }"));
        rsp_return = qdict_get_qdict(rsp, "return");
        status = qdict_get_str(rsp_return, "status");
        completed = strcmp(status, "completed") == 0;
        g_assert_cmpstr(status, !=,  "failed");
        QDECREF(rsp);
        usleep(1000 * 100);
    } while (!completed);
}

static void cleanup(const char *filename)
{
    char *path = g_strdup_printf("%s/%s", tmpfs, filename);

    unlink(path);
    g_free(path);
}

static void test_migrate(void)
{
    QTestState *global = global_qtest, *from, *to;
    gchar *cmd, *cmd_dst;
    QDict *rsp;
    struct stat packet_stat;
    char *migpath = g_strdup_printf("%s/migstream", tmpfs);
    char *packetpath = g_strdup_printf("%s/packets", tmpfs);

    from = qtest_start("-m 2M -name source,debug-threads=on "
                       "-nographic -nodefaults "
                      "-netdev user,id=netuser "
                       " -device e1000,netdev=netuser,mac=00:11:22:33:44:55");

    global_qtest = from;
    cmd = g_strdup_printf("{ 'execute': 'migrate',"
                          "'arguments': { 'uri': 'exec:cat > %s' } }",
                          migpath);
    rsp = qmp(cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    wait_for_migration_complete();

    cmd_dst = g_strdup_printf("-m 2M -name dest,debug-threads=on "
                      "-nographic -nodefaults "
                      "-netdev user,id=netuser "
                      "-object filter-dump,id=dump,netdev=netuser,file=%s "
                      "-device e1000,netdev=netuser,mac=00:11:22:33:44:55 "
                      " -incoming defer",
                      packetpath);

    to = qtest_start(cmd_dst);
    g_free(cmd_dst);

    rsp = qmp("{ 'execute': 'migrate-set-parameters',"
                  "'arguments': { "
                      " 'announce-rounds': 6, "
                      " 'announce-initial': 10, "
                      " 'announce-max': 100, "
                      " 'announce-step': 40 } }");
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    cmd_dst = g_strdup_printf("{ 'execute': 'migrate-incoming',"
                  "'arguments': { 'uri': 'exec:cat %s' } }",
                  migpath);
    rsp = return_or_event(qmp(cmd_dst));
    g_free(cmd_dst);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    qmp_eventwait("RESUME");

    /* Sleep for a while to let that announce happens,
     * it should be <p> 10ms <p> 50ms <p> 90ms <p> 100ms <p> 100ms <p>
     * so that's at least 350ms but lets assume we're on a bit of a
     * loaded host and give it a bit longer
     */
    sleep(2);
    qtest_quit(from);
    qtest_quit(to);

    g_assert_cmpint(stat(packetpath, &packet_stat), ==, 0);
    /* 480 bytes for 6 packets */
    g_assert_cmpint(packet_stat.st_size, ==, 480);

    global_qtest = global;

    cleanup("packetpath");
    cleanup("migpath");
}

int main(int argc, char **argv)
{
    char template[] = "/tmp/announce-test-XXXXXX";
    int ret;

    g_test_init(&argc, &argv, NULL);

    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path (%s): %s\n", template, strerror(errno));
    }
    g_assert(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    qtest_add_func("/announce", test_migrate);

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s\n",
                       tmpfs, strerror(errno));
    }

    return ret;
}
