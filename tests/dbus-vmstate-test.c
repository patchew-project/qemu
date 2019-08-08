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
    GDBusObjectManagerServer *om;
    GDBusServer *server;
} TestServer;

typedef struct Test {
    bool migrate_fail;
    TestServer srcA;
    TestServer dstA;
    TestServer srcB;
    TestServer dstB;
    GMainLoop *loop, *dbus_loop;
    QTestState *src_qemu;
} Test;

GMutex mutex;
GCond cond;

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

static void
connection_closed(GDBusConnection *connection,
                  gboolean remote_peer_vanished,
                  GError *Error,
                  gpointer user_data)
{
    TestServer *h = user_data;

    g_clear_object(&h->om);
    g_clear_object(&connection);
}

static GDBusObjectManagerServer *
get_omserver(GDBusConnection *conn, gpointer user_data)
{
    TestServer *h = user_data;
    GDBusObjectManagerServer *om;
    GDBusObjectSkeleton *sk;
    VMState1 *v;

    om = g_dbus_object_manager_server_new("/org/qemu");
    sk = g_dbus_object_skeleton_new("/org/qemu/VMState1");

    v = vmstate1_skeleton_new();
    g_object_set(v, "id", h->id->name, NULL);
    g_signal_connect(v, "handle-load", G_CALLBACK(vmstate_load), user_data);
    g_signal_connect(v, "handle-save", G_CALLBACK(vmstate_save), user_data);

    g_dbus_object_skeleton_add_interface(sk, G_DBUS_INTERFACE_SKELETON(v));
    g_dbus_object_manager_server_export(om, sk);
    g_dbus_object_manager_server_set_connection(om, conn);

    g_clear_object(&v);
    g_clear_object(&sk);

    return om;
}

static gboolean
on_new_connection(GDBusServer *server,
                  GDBusConnection *connection,
                  gpointer user_data)
{
    TestServer *h = user_data;

    g_object_ref(connection);
    g_signal_connect(connection, "closed",
                     G_CALLBACK(connection_closed), user_data);
    h->om = get_omserver(connection, user_data);

    return TRUE;
}

static gboolean
allow_mechanism_cb(GDBusAuthObserver *observer,
                   const gchar *mechanism,
                   gpointer user_data)
{
    return g_strcmp0(mechanism, "EXTERNAL") == 0;
}

static gboolean
authorize_authenticated_peer_cb(GDBusAuthObserver *observer,
                                GIOStream *stream,
                                GCredentials *credentials,
                                gpointer user_data)
{
    gboolean authorized = FALSE;

    if (credentials != NULL) {
        GCredentials *own_credentials = g_credentials_new();

        if (g_credentials_is_same_user(credentials, own_credentials, NULL)) {
            authorized = TRUE;
        }

        g_clear_object(&own_credentials);
    }

    return authorized;
}

static GDBusServer *
server_start(TestServer *h, const char *p, const TestServerId *id)
{
    GDBusAuthObserver *observer = NULL;
    GDBusServer *server = NULL;
    gchar *guid = NULL;
    GError *error = NULL;
    char *addr = NULL;

    h->id = id;
    addr = g_strdup_printf("unix:path=%s/dbus-%s%s", workdir, p, h->id->name);
    guid = g_dbus_generate_guid();
    observer = g_dbus_auth_observer_new();
    g_signal_connect(observer, "allow-mechanism",
                     G_CALLBACK(allow_mechanism_cb), h);
    g_signal_connect(observer, "authorize-authenticated-peer",
                     G_CALLBACK(authorize_authenticated_peer_cb), h);

    server = g_dbus_server_new_sync(addr,
                                    G_DBUS_SERVER_FLAGS_NONE,
                                    guid,
                                    observer,
                                    NULL, /* GCancellable */
                                    &error);
    g_dbus_server_start(server);
    g_clear_object(&observer);
    g_free(guid);

    if (server == NULL) {
        g_printerr("Error creating server at address %s: %s\n",
                   addr, error->message);
        g_error_free(error);
        return NULL;
    }

    g_signal_connect(server, "new-connection",
                     G_CALLBACK(on_new_connection), h);

    g_free(addr);
    return server;
}


static gpointer
dbus_thread(gpointer p)
{
    Test *test = p;
    GMainContext *context = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(context, FALSE);

    g_main_context_push_thread_default(context);

    g_mutex_lock(&mutex);
    test->srcA.server = server_start(&test->srcA, "src", &idA);
    test->srcB.server = server_start(&test->srcB, "src", &idB);
    test->dstA.server = server_start(&test->dstA, "dst", &idA);
    test->dstB.server = server_start(&test->dstB, "dst", &idB);
    test->dbus_loop = loop;
    g_cond_signal(&cond);
    g_mutex_unlock(&mutex);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_main_context_unref(context);

    g_mutex_lock(&mutex);
    g_clear_object(&test->srcA.server);
    g_clear_object(&test->srcB.server);
    g_clear_object(&test->dstA.server);
    g_clear_object(&test->dstB.server);
    g_mutex_unlock(&mutex);

    return NULL;
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

static void
test_dbus_vmstate(Test *test)
{
    QTestState *src_qemu = NULL, *dst_qemu = NULL;
    char *src_qemu_args = NULL, *dst_qemu_args = NULL;
    char *uri = g_strdup_printf("unix:%s/migsocket", workdir);
    GThread *t = g_thread_new("dbus", dbus_thread, test);

    g_mutex_lock(&mutex);
    while (!test->dbus_loop) {
        g_cond_wait(&cond, &mutex);
    }

    src_qemu_args =
        g_strdup_printf("-object dbus-vmstate,id=dvA,addr=%s "
                        "-object dbus-vmstate,id=dvB,addr=%s",
                        g_dbus_server_get_client_address(test->srcA.server),
                        g_dbus_server_get_client_address(test->srcB.server));


    dst_qemu_args =
        g_strdup_printf("-object dbus-vmstate,id=dvA,addr=%s "
                        "-object dbus-vmstate,id=dvB,addr=%s "
                        "-incoming %s",
                        g_dbus_server_get_client_address(test->dstA.server),
                        g_dbus_server_get_client_address(test->dstB.server),
                        uri);

    src_qemu = qtest_init(src_qemu_args);
    dst_qemu = qtest_init(dst_qemu_args);

    test->loop = g_main_loop_new(NULL, TRUE);

    migrate(src_qemu, uri);
    test->src_qemu = src_qemu;
    g_timeout_add_seconds(1, wait_for_migration_complete, test);

    g_main_loop_run(test->loop);
    g_main_loop_unref(test->loop);

    g_free(uri);
    qtest_quit(dst_qemu);
    qtest_quit(src_qemu);
    g_free(dst_qemu_args);
    g_free(src_qemu_args);

    g_main_loop_quit(test->dbus_loop);
    g_mutex_unlock(&mutex);

    g_thread_join(t);
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
test_dbus_vmstate_migrate(void)
{
    Test test = { };

    test_dbus_vmstate(&test);

    check_migrated(&test.srcA, &test.dstA);
    check_migrated(&test.srcB, &test.dstB);
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

    qtest_add_func("/dbus-vmstate/migrate",
                   test_dbus_vmstate_migrate);

    ret = g_test_run();

    rmdir(workdir);
    g_free(workdir);

    return ret;
}
