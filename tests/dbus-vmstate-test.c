#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include <gio/gio.h>
#include "libqtest.h"
#include "qemu-common.h"
#include "dbus-vmstate1.h"

static char *workdir;

typedef struct TestServerId {
    const char *name;
    const char *data;
    size_t size;
} TestServerId;

static const TestServerId idA = {
    "idA", "I'am\0idA!", sizeof("I'am\0idA!")
};

static const TestServerId idB = {
    "idB", "I'am\0idB!", sizeof("I'am\0idB!")
};

typedef struct TestServer {
    const TestServerId *id;
    bool save_called;
    bool load_called;
} TestServer;

typedef struct Test {
    const char *id_list;
    bool migrate_fail;
    bool without_dst_b;
    TestServer srcA;
    TestServer dstA;
    TestServer srcB;
    TestServer dstB;
    GMainLoop *loop;
    QTestState *src_qemu;
} Test;

static gboolean
vmstate_load(VMState1 *object, GDBusMethodInvocation *invocation,
             const gchar *arg_data, gpointer user_data)
{
    TestServer *h = user_data;
    GVariant *args, *var;
    const uint8_t *data;
    size_t size;

    args = g_dbus_method_invocation_get_parameters(invocation);
    var = g_variant_get_child_value(args, 0);
    data = g_variant_get_fixed_array(var, &size, sizeof(char));
    g_assert_cmpuint(size, ==, h->id->size);
    g_assert(!memcmp(data, h->id->data, h->id->size));
    h->load_called = true;
    g_variant_unref(var);

    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    return TRUE;
}

static gboolean
vmstate_save(VMState1 *object, GDBusMethodInvocation *invocation,
             gpointer user_data)
{
    TestServer *h = user_data;
    GVariant *var;

    var = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                    h->id->data, h->id->size, sizeof(char));
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(@ay)", var));
    h->save_called = true;

    return TRUE;
}

static gboolean
wait_for_migration_complete(gpointer user_data)
{
    Test *test = user_data;
    QDict *rsp_return;
    bool stop = false;
    const char *status;

    qtest_qmp_send(test->src_qemu, "{ 'execute': 'query-migrate' }");
    rsp_return = qtest_qmp_receive_success(test->src_qemu, NULL, NULL);
    status = qdict_get_str(rsp_return, "status");
    if (g_str_equal(status, "completed") || g_str_equal(status, "failed")) {
        stop = true;
        g_assert_cmpstr(status, ==,
                        test->migrate_fail ? "failed" : "completed");
    }
    qobject_unref(rsp_return);

    if (stop) {
        g_main_loop_quit(test->loop);
    }
    return stop ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static void migrate(QTestState *who, const char *uri)
{
    QDict *args, *rsp;

    args = qdict_new();
    qdict_put_str(args, "uri", uri);

    rsp = qtest_qmp(who, "{ 'execute': 'migrate', 'arguments': %p }", args);

    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
}

typedef struct WaitNamed {
    GMainLoop *loop;
    bool named;
} WaitNamed;

static void
named_cb(GDBusConnection *connection,
         const gchar *name,
         gpointer user_data)
{
    WaitNamed *t = user_data;

    t->named = true;
    g_main_loop_quit(t->loop);
}

static GDBusConnection *
get_connection(Test *test, guint *ownid)
{
    WaitNamed *wait = g_new0(WaitNamed, 1);
    GError *err = NULL;
    GDBusConnection *c;
    gchar *addr;

    wait->loop = test->loop;
    addr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, &err);
    g_assert_no_error(err);

    c = g_dbus_connection_new_for_address_sync(
        addr,
        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
        NULL, NULL, &err);
    g_assert_no_error(err);
    *ownid = g_bus_own_name_on_connection(c, "org.qemu.VMState1",
                                          G_BUS_NAME_OWNER_FLAGS_NONE,
                                          named_cb, named_cb, wait, g_free);
    if (!wait->named) {
        g_main_loop_run(wait->loop);
    }

    g_free(addr);
    return c;
}

static GDBusObjectManagerServer *
get_server(GDBusConnection *conn, TestServer *s, const TestServerId *id)
{
    GDBusObjectManagerServer *os;
    GDBusObjectSkeleton *sk;
    VMState1 *v;

    s->id = id;
    os = g_dbus_object_manager_server_new("/org/qemu");
    sk = g_dbus_object_skeleton_new("/org/qemu/VMState1");

    v = vmstate1_skeleton_new();
    g_object_set(v, "id", id->name, NULL);

    g_signal_connect(v, "handle-load", G_CALLBACK(vmstate_load), s);
    g_signal_connect(v, "handle-save", G_CALLBACK(vmstate_save), s);

    g_dbus_object_skeleton_add_interface(sk, G_DBUS_INTERFACE_SKELETON(v));
    g_dbus_object_manager_server_export(os, sk);
    g_dbus_object_manager_server_set_connection(os, conn);

    g_clear_object(&v);
    g_clear_object(&sk);

    return os;
}

static void
set_id_list(Test *test, QTestState *s)
{
    if (!test->id_list) {
        return;
    }

    g_assert(!qmp_rsp_is_err(qtest_qmp(s,
        "{ 'execute': 'qom-set', 'arguments': "
        "{ 'path': '/objects/dv', 'property': 'id-list', 'value': %s } }",
        test->id_list)));
}
static void
test_dbus_vmstate(Test *test)
{
    QTestState *src_qemu = NULL, *dst_qemu = NULL;
    char *src_qemu_args = NULL, *dst_qemu_args = NULL;
    GTestDBus *srcbus = NULL, *dstbus = NULL;
    GDBusConnection *srcconnA = NULL, *srcconnB = NULL;
    GDBusConnection *dstconnA = NULL, *dstconnB = NULL;
    guint ownsrcA, ownsrcB, owndstA, owndstB;
    GDBusObjectManagerServer *srcserverA = NULL, *srcserverB = NULL;
    GDBusObjectManagerServer *dstserverA = NULL, *dstserverB = NULL;
    char **srcaddr = NULL, **dstaddr = NULL;
    char *uri = g_strdup_printf("unix:%s/migsocket", workdir);

    test->loop = g_main_loop_new(NULL, TRUE);

    srcbus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(srcbus);
    srcconnA = get_connection(test, &ownsrcA);
    srcserverA = get_server(srcconnA, &test->srcA, &idA);
    srcconnB = get_connection(test, &ownsrcB);
    srcserverB = get_server(srcconnB, &test->srcB, &idB);

    /* remove ,guid=foo part */
    srcaddr = g_strsplit(g_test_dbus_get_bus_address(srcbus), ",", 2);
    src_qemu_args =
        g_strdup_printf("-object dbus-vmstate,id=dv,addr=%s", srcaddr[0]);

    dstbus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(dstbus);
    dstconnA = get_connection(test, &owndstA);
    dstserverA = get_server(dstconnA, &test->dstA, &idA);
    if (!test->without_dst_b) {
        dstconnB = get_connection(test, &owndstB);
        dstserverB = get_server(dstconnB, &test->dstB, &idB);
    }

    dstaddr = g_strsplit(g_test_dbus_get_bus_address(dstbus), ",", 2);
    dst_qemu_args =
        g_strdup_printf("-object dbus-vmstate,id=dv,addr=%s -incoming %s",
                        dstaddr[0], uri);

    src_qemu = qtest_init(src_qemu_args);
    dst_qemu = qtest_init(dst_qemu_args);
    set_id_list(test, src_qemu);
    set_id_list(test, dst_qemu);

    migrate(src_qemu, uri);
    test->src_qemu = src_qemu;
    g_timeout_add_seconds(1, wait_for_migration_complete, test);

    g_main_loop_run(test->loop);
    g_main_loop_unref(test->loop);

    g_free(uri);
    if (test->migrate_fail) {
        qtest_set_exit_status(dst_qemu, 1);
    }
    qtest_quit(dst_qemu);
    qtest_quit(src_qemu);
    g_free(dst_qemu_args);
    g_free(src_qemu_args);
    g_bus_unown_name(ownsrcA);
    g_bus_unown_name(ownsrcB);
    g_bus_unown_name(owndstA);
    if (!test->without_dst_b) {
        g_bus_unown_name(owndstB);
    }
    g_clear_object(&srcbus);
    g_clear_object(&dstbus);
    g_clear_object(&srcserverA);
    g_clear_object(&dstserverA);
    g_clear_object(&srcserverB);
    g_clear_object(&dstserverB);
    g_clear_object(&srcconnA);
    g_clear_object(&dstconnA);
    g_clear_object(&srcconnB);
    g_clear_object(&dstconnB);
    g_strfreev(srcaddr);
    g_strfreev(dstaddr);
}

static void
check_not_migrated(TestServer *s, TestServer *d)
{
    assert(!s->save_called);
    assert(!s->load_called);
    assert(!d->save_called);
    assert(!d->load_called);
}

static void
check_migrated(TestServer *s, TestServer *d)
{
    assert(s->save_called);
    assert(!s->load_called);
    assert(!d->save_called);
    assert(d->load_called);
}

static void
test_dbus_vmstate_without_list(void)
{
    Test test = { 0, };

    test_dbus_vmstate(&test);

    check_migrated(&test.srcA, &test.dstA);
    check_migrated(&test.srcB, &test.dstB);
}

static void
test_dbus_vmstate_with_list(void)
{
    Test test = { .id_list = "idA,idB" };

    test_dbus_vmstate(&test);

    check_migrated(&test.srcA, &test.dstA);
    check_migrated(&test.srcB, &test.dstB);
}

static void
test_dbus_vmstate_only_a(void)
{
    Test test = { .id_list = "idA" };

    test_dbus_vmstate(&test);

    check_migrated(&test.srcA, &test.dstA);
    check_not_migrated(&test.srcB, &test.dstB);
}

static void
test_dbus_vmstate_missing_src(void)
{
    Test test = { .id_list = "idA,idC", .migrate_fail = true };

    test_dbus_vmstate(&test);
    check_not_migrated(&test.srcA, &test.dstA);
    check_not_migrated(&test.srcB, &test.dstB);
}

static void
test_dbus_vmstate_missing_dst(void)
{
    Test test = { .id_list = "idA,idB",
                  .without_dst_b = true,
                  .migrate_fail = true };

    test_dbus_vmstate(&test);

    assert(test.srcA.save_called);
    assert(test.srcB.save_called);
    assert(!test.dstB.save_called);
}

int
main(int argc, char **argv)
{
    GError *err = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    workdir = g_dir_make_tmp("dbus-vmstate-test-XXXXXX", &err);
    if (!workdir) {
        g_error("Unable to create temporary dir: %s\n", err->message);
    }

    qtest_add_func("/dbus-vmstate/without-list",
                   test_dbus_vmstate_without_list);
    qtest_add_func("/dbus-vmstate/with-list",
                   test_dbus_vmstate_with_list);
    qtest_add_func("/dbus-vmstate/only-a",
                   test_dbus_vmstate_only_a);
    qtest_add_func("/dbus-vmstate/missing-src",
                   test_dbus_vmstate_missing_src);
    qtest_add_func("/dbus-vmstate/missing-dst",
                   test_dbus_vmstate_missing_dst);

    ret = g_test_run();

    rmdir(workdir);
    g_free(workdir);

    return ret;
}
