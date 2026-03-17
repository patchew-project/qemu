/*
 * D-Bus VNC server (qemu-vnc) end-to-end test
 *
 * Starts QEMU with D-Bus display, connects qemu-vnc via p2p,
 * then verifies a gvnc client can connect and read the VM name.
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <gio/gio.h>
#include <gvnc.h>
#include <sys/un.h>
#include "qemu/sockets.h"
#include "libqtest.h"
#include "qemu-vnc1.h"

typedef struct Test {
    QTestState *qts;
    GSubprocess *vnc_subprocess;
    VncConnection *conn;
    GMainLoop *loop;
    char *vnc_sock_path;
    char *tmp_dir;
} Test;

typedef struct DbusTest {
    QTestState *qts;
    GSubprocess *vnc_subprocess;
    GTestDBus *bus;
    GDBusConnection *bus_conn;
    GMainLoop *loop;
    char *vnc_sock_path;
    char *tmp_dir;
    char *bus_addr;
} DbusTest;

typedef struct LifecycleData {
    DbusTest *dt;
    QemuVnc1Server *server_proxy;
    VncConnection *conn;
    char *client_path;
    gboolean got_connected;
    gboolean got_initialized;
    gboolean got_disconnected;
} LifecycleData;

static void
on_vnc_error(VncConnection *self, const char *msg)
{
    g_error("vnc-error: %s", msg);
}

static void
on_vnc_auth_failure(VncConnection *self, const char *msg)
{
    g_error("vnc-auth-failure: %s", msg);
}

static void
on_vnc_initialized(VncConnection *self, Test *test)
{
    const char *name = vnc_connection_get_name(test->conn);

    g_assert_cmpstr(name, ==, "QEMU (dbus-vnc-test)");
    g_main_loop_quit(test->loop);
}

static gboolean
timeout_cb(gpointer data)
{
    g_error("test timed out");
    return G_SOURCE_REMOVE;
}

static int
connect_unix_socket(const char *path)
{
    int fd;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert(fd >= 0);

    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool
wait_for_vnc_socket(const char *path, int timeout_ms)
{
    int elapsed = 0;
    const int interval = 50;

    while (elapsed < timeout_ms) {
        int fd = connect_unix_socket(path);

        if (fd >= 0) {
            close(fd);
            return true;
        }

        g_usleep(interval * 1000);
        elapsed += interval;
    }
    return false;
}

static GSubprocess *
spawn_qemu_vnc(int dbus_fd, const char *sock_path)
{
    const char *binary;
    g_autoptr(GError) err = NULL;
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    GSubprocess *proc;
    g_autofree char *fd_str = NULL;
    g_autofree char *vnc_addr = NULL;

    binary = g_getenv("QTEST_QEMU_VNC_BINARY");
    g_assert(binary != NULL);

    fd_str = g_strdup_printf("%d", dbus_fd);
    vnc_addr = g_strdup_printf("unix:%s", sock_path);

    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_fd(launcher, dbus_fd, dbus_fd);

    proc = g_subprocess_launcher_spawn(launcher, &err,
                                       binary,
                                       "--dbus-p2p-fd", fd_str,
                                       "--vnc-addr", vnc_addr,
                                       NULL);
    g_assert_no_error(err);
    g_assert(proc != NULL);

    return proc;
}

static GSubprocess *
spawn_qemu_vnc_bus_full(const char *dbus_addr, const char *sock_path,
                        const char *const *extra_args)
{
    const char *binary;
    g_autoptr(GError) err = NULL;
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    g_autoptr(GPtrArray) argv = NULL;
    GSubprocess *proc;
    g_autofree char *vnc_addr = NULL;

    binary = g_getenv("QTEST_QEMU_VNC_BINARY");
    g_assert(binary != NULL);

    vnc_addr = g_strdup_printf("unix:%s", sock_path);

    argv = g_ptr_array_new();
    g_ptr_array_add(argv, (gpointer)binary);
    g_ptr_array_add(argv, (gpointer)"--dbus-address");
    g_ptr_array_add(argv, (gpointer)dbus_addr);
    g_ptr_array_add(argv, (gpointer)"--bus-name");
    g_ptr_array_add(argv, (gpointer)"org.qemu");
    g_ptr_array_add(argv, (gpointer)"--vnc-addr");
    g_ptr_array_add(argv, (gpointer)vnc_addr);

    if (extra_args) {
        for (int i = 0; extra_args[i]; i++) {
            g_ptr_array_add(argv, (gpointer)extra_args[i]);
        }
    }

    g_ptr_array_add(argv, NULL);

    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    proc = g_subprocess_launcher_spawnv(launcher, (const char *const *)argv->pdata, &err);
    g_assert_no_error(err);
    g_assert(proc != NULL);

    return proc;
}


static void
name_appeared_cb(GDBusConnection *connection,
                 const gchar *name,
                 const gchar *name_owner,
                 gpointer user_data)
{
    gboolean *appeared = user_data;
    *appeared = TRUE;
}

static bool
setup_dbus_test_full(DbusTest *dt, const char *const *vnc_extra_args)
{
    g_autoptr(GError) err = NULL;
    g_auto(GStrv) addr_parts = NULL;
    g_autofree char *qemu_args = NULL;

    if (!g_getenv("QTEST_QEMU_VNC_BINARY")) {
        g_test_skip("QTEST_QEMU_VNC_BINARY not set");
        return false;
    }

    dt->bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(dt->bus);

    /* remove ,guid=foo part */
    addr_parts = g_strsplit(g_test_dbus_get_bus_address(dt->bus), ",", 2);
    dt->bus_addr = g_strdup(addr_parts[0]);

    dt->bus_conn = g_dbus_connection_new_for_address_sync(
        g_test_dbus_get_bus_address(dt->bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, &err);
    g_assert_no_error(err);

    qemu_args = g_strdup_printf("-display dbus,addr=%s "
                                "-name dbus-vnc-test", dt->bus_addr);
    dt->qts = qtest_init(qemu_args);

    dt->tmp_dir = g_dir_make_tmp("dbus-vnc-test-XXXXXX", NULL);
    g_assert(dt->tmp_dir != NULL);
    dt->vnc_sock_path = g_build_filename(dt->tmp_dir, "vnc.sock", NULL);
    dt->vnc_subprocess = spawn_qemu_vnc_bus_full(dt->bus_addr,
                                                 dt->vnc_sock_path,
                                                 vnc_extra_args);

    /*
     * Wait for the org.qemu.vnc bus name to appear, which indicates
     * qemu-vnc has fully initialized (connected to QEMU, set up the
     * display, exported its D-Bus interfaces, and opened the VNC
     * socket).
     */
    {
        guint watch_id, timeout_id;
        gboolean appeared = FALSE;

        watch_id = g_bus_watch_name_on_connection(
            dt->bus_conn, "org.qemu.vnc",
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            name_appeared_cb, NULL, &appeared, NULL);
        timeout_id = g_timeout_add_seconds(10, timeout_cb, NULL);

        while (!appeared &&
               g_main_context_iteration(NULL, TRUE)) {
            /* spin until name appears or timeout fires */
        }

        g_bus_unwatch_name(watch_id);
        g_source_remove(timeout_id);

        if (!appeared) {
            g_test_fail();
            g_test_message("Timed out waiting for org.qemu.vnc bus name");
            return false;
        }
    }

    return true;
}

static bool
setup_dbus_test(DbusTest *dt)
{
    return setup_dbus_test_full(dt, NULL);
}

static void
cleanup_dbus_test(DbusTest *dt)
{
    if (dt->bus_conn) {
        g_dbus_connection_close_sync(dt->bus_conn, NULL, NULL);
        g_object_unref(dt->bus_conn);
    }
    if (dt->vnc_subprocess) {
        g_subprocess_force_exit(dt->vnc_subprocess);
        g_subprocess_wait(dt->vnc_subprocess, NULL, NULL);
        g_object_unref(dt->vnc_subprocess);
    }
    if (dt->vnc_sock_path) {
        unlink(dt->vnc_sock_path);
        g_free(dt->vnc_sock_path);
    }
    if (dt->tmp_dir) {
        rmdir(dt->tmp_dir);
        g_free(dt->tmp_dir);
    }
    if (dt->qts) {
        qtest_quit(dt->qts);
    }
    if (dt->bus) {
        g_test_dbus_down(dt->bus);
        g_object_unref(dt->bus);
    }
    g_free(dt->bus_addr);
}

static void
test_dbus_vnc_basic(void)
{
    Test test = { 0 };
    int pair[2];
    int vnc_fd;
    guint timeout_id;

    if (!g_getenv("QTEST_QEMU_VNC_BINARY")) {
        g_test_skip("QTEST_QEMU_VNC_BINARY not set");
        return;
    }

    test.qts = qtest_init("-display dbus,p2p=yes -name dbus-vnc-test");

    g_assert_cmpint(qemu_socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    qtest_qmp_add_client(test.qts, "@dbus-display", pair[1]);
    close(pair[1]);

    test.tmp_dir = g_dir_make_tmp("dbus-vnc-test-XXXXXX", NULL);
    g_assert(test.tmp_dir != NULL);
    test.vnc_sock_path = g_build_filename(test.tmp_dir, "vnc.sock", NULL);

    test.vnc_subprocess = spawn_qemu_vnc(pair[0], test.vnc_sock_path);

    if (!wait_for_vnc_socket(test.vnc_sock_path, 10000)) {
        g_test_fail();
        g_test_message("Timed out waiting for qemu-vnc socket");
        goto cleanup;
    }

    vnc_fd = connect_unix_socket(test.vnc_sock_path);
    g_assert(vnc_fd >= 0);

    test.conn = vnc_connection_new();
    g_signal_connect(test.conn, "vnc-error",
                     G_CALLBACK(on_vnc_error), NULL);
    g_signal_connect(test.conn, "vnc-auth-failure",
                     G_CALLBACK(on_vnc_auth_failure), NULL);
    g_signal_connect(test.conn, "vnc-initialized",
                     G_CALLBACK(on_vnc_initialized), &test);
    vnc_connection_set_auth_type(test.conn, VNC_CONNECTION_AUTH_NONE);
    vnc_connection_open_fd(test.conn, vnc_fd);

    test.loop = g_main_loop_new(NULL, FALSE);
    timeout_id = g_timeout_add_seconds(10, timeout_cb, NULL);
    g_main_loop_run(test.loop);
    g_source_remove(timeout_id);

cleanup:
    if (test.conn) {
        vnc_connection_shutdown(test.conn);
        g_signal_handlers_disconnect_by_data(test.conn, NULL);
        g_object_unref(test.conn);
    }
    if (test.loop) {
        g_main_loop_unref(test.loop);
    }
    if (test.vnc_subprocess) {
        g_subprocess_force_exit(test.vnc_subprocess);
        g_subprocess_wait(test.vnc_subprocess, NULL, NULL);
        g_object_unref(test.vnc_subprocess);
    }
    if (test.vnc_sock_path) {
        unlink(test.vnc_sock_path);
        g_free(test.vnc_sock_path);
    }
    if (test.tmp_dir) {
        rmdir(test.tmp_dir);
        g_free(test.tmp_dir);
    }
    qtest_quit(test.qts);
}

static void
test_dbus_vnc_server_props(void)
{
    DbusTest dt = { 0 };
    QemuVnc1Server *proxy = NULL;
    g_autoptr(GError) err = NULL;
    const gchar *const *clients;
    GVariant *listeners;

    if (!setup_dbus_test(&dt)) {
        goto cleanup;
    }

    proxy = qemu_vnc1_server_proxy_new_sync(
        dt.bus_conn,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.qemu.vnc",
        "/org/qemu/Vnc1/Server",
        NULL, &err);
    g_assert_no_error(err);
    g_assert(proxy != NULL);

    g_assert_cmpstr(qemu_vnc1_server_get_name(proxy), ==,
                    "dbus-vnc-test");
    g_assert_cmpstr(qemu_vnc1_server_get_auth(proxy), ==,
                    "none");
    g_assert_cmpstr(qemu_vnc1_server_get_vencrypt_sub_auth(proxy), ==,
                    "");

    clients = qemu_vnc1_server_get_clients(proxy);
    g_assert_nonnull(clients);
    g_assert_cmpint(g_strv_length((gchar **)clients), ==, 0);

    listeners = qemu_vnc1_server_get_listeners(proxy);
    g_assert_nonnull(listeners);
    g_assert_cmpint(g_variant_n_children(listeners), >, 0);

cleanup:
    g_clear_object(&proxy);
    cleanup_dbus_test(&dt);
}

static void
on_client_connected(QemuVnc1Server *proxy,
                    const gchar *client_path,
                    LifecycleData *data)
{
    data->got_connected = TRUE;
    data->client_path = g_strdup(client_path);
}

static void
on_lifecycle_vnc_initialized(VncConnection *self, LifecycleData *data)
{
    /* VNC handshake done, wait for ClientInitialized D-Bus signal */
}

static void
on_client_initialized(QemuVnc1Server *proxy,
                      const gchar *client_path,
                      LifecycleData *data)
{
    data->got_initialized = TRUE;
    g_main_loop_quit(data->dt->loop);
}

static void
on_client_disconnected(QemuVnc1Server *proxy,
                       const gchar *client_path,
                       LifecycleData *data)
{
    data->got_disconnected = TRUE;
    g_main_loop_quit(data->dt->loop);
}

static void
test_dbus_vnc_client_lifecycle(void)
{
    DbusTest dt = { 0 };
    QemuVnc1Server *server_proxy = NULL;
    QemuVnc1Client *client_proxy = NULL;
    g_autoptr(GError) err = NULL;
    LifecycleData ldata = { 0 };
    int vnc_fd;
    guint timeout_id;

    if (!setup_dbus_test(&dt)) {
        goto cleanup;
    }

    server_proxy = qemu_vnc1_server_proxy_new_sync(
        dt.bus_conn,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.qemu.vnc",
        "/org/qemu/Vnc1/Server",
        NULL, &err);
    g_assert_no_error(err);

    ldata.dt = &dt;
    ldata.server_proxy = server_proxy;

    g_signal_connect(server_proxy, "client-connected",
                     G_CALLBACK(on_client_connected), &ldata);
    g_signal_connect(server_proxy, "client-initialized",
                     G_CALLBACK(on_client_initialized), &ldata);
    g_signal_connect(server_proxy, "client-disconnected",
                     G_CALLBACK(on_client_disconnected), &ldata);

    vnc_fd = connect_unix_socket(dt.vnc_sock_path);
    g_assert(vnc_fd >= 0);

    ldata.conn = vnc_connection_new();
    g_signal_connect(ldata.conn, "vnc-error",
                     G_CALLBACK(on_vnc_error), NULL);
    g_signal_connect(ldata.conn, "vnc-auth-failure",
                     G_CALLBACK(on_vnc_auth_failure), NULL);
    g_signal_connect(ldata.conn, "vnc-initialized",
                     G_CALLBACK(on_lifecycle_vnc_initialized), &ldata);
    vnc_connection_set_auth_type(ldata.conn, VNC_CONNECTION_AUTH_NONE);
    vnc_connection_open_fd(ldata.conn, vnc_fd);

    /* Phase 1: wait for ClientInitialized */
    dt.loop = g_main_loop_new(NULL, FALSE);
    timeout_id = g_timeout_add_seconds(10, timeout_cb, NULL);
    g_main_loop_run(dt.loop);
    g_source_remove(timeout_id);

    g_assert_true(ldata.got_connected);
    g_assert_true(ldata.got_initialized);
    g_assert_nonnull(ldata.client_path);

    /* Check client properties while still connected */
    client_proxy = qemu_vnc1_client_proxy_new_sync(
        dt.bus_conn,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.qemu.vnc",
        ldata.client_path,
        NULL, &err);
    g_assert_no_error(err);

    g_assert_cmpstr(qemu_vnc1_client_get_family(client_proxy), ==,
                    "unix");
    g_assert_false(qemu_vnc1_client_get_web_socket(client_proxy));
    g_assert_cmpstr(qemu_vnc1_client_get_x509_dname(client_proxy), ==,
                    "");
    g_assert_cmpstr(qemu_vnc1_client_get_sasl_username(client_proxy),
                    ==, "");

    /* Phase 2: disconnect and wait for ClientDisconnected */
    vnc_connection_shutdown(ldata.conn);
    timeout_id = g_timeout_add_seconds(10, timeout_cb, NULL);
    g_main_loop_run(dt.loop);
    g_source_remove(timeout_id);

    g_assert_true(ldata.got_disconnected);

    g_object_unref(ldata.conn);
    g_main_loop_unref(dt.loop);
    dt.loop = NULL;
    g_free(ldata.client_path);

cleanup:
    g_clear_object(&server_proxy);
    g_clear_object(&client_proxy);
    cleanup_dbus_test(&dt);
}

static void
test_dbus_vnc_no_password(void)
{
    DbusTest dt = { 0 };
    QemuVnc1Server *proxy = NULL;
    g_autoptr(GError) err = NULL;
    gboolean ret;

    if (!setup_dbus_test(&dt)) {
        goto cleanup;
    }

    proxy = qemu_vnc1_server_proxy_new_sync(
        dt.bus_conn,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.qemu.vnc",
        "/org/qemu/Vnc1/Server",
        NULL, &err);
    g_assert_no_error(err);

    /*
     * With default auth=none, SetPassword should return an error
     * because VNC password authentication is not enabled.
     */
    ret = qemu_vnc1_server_call_set_password_sync(
        proxy, "secret",
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    g_assert_false(ret);
    g_assert_error(err, G_DBUS_ERROR, G_DBUS_ERROR_FAILED);
    g_clear_error(&err);

    /*
     * ExpirePassword succeeds even without password auth —
     * it just sets the expiry timestamp.
     */
    ret = qemu_vnc1_server_call_expire_password_sync(
        proxy, "never",
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(ret);

    ret = qemu_vnc1_server_call_expire_password_sync(
        proxy, "+3600",
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(ret);

cleanup:
    g_clear_object(&proxy);
    cleanup_dbus_test(&dt);
}

typedef struct PasswordData {
    DbusTest *dt;
    VncConnection *conn;
    const char *password;
    gboolean auth_succeeded;
    gboolean auth_failed;
} PasswordData;

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
on_pw_vnc_auth_credential(VncConnection *conn, GValueArray *creds,
                          PasswordData *data)
{
    for (guint i = 0; i < creds->n_values; i++) {
        int type = g_value_get_enum(g_value_array_get_nth(creds, i));

        if (type == VNC_CONNECTION_CREDENTIAL_PASSWORD) {
            vnc_connection_set_credential(conn, type, data->password);
        }
    }
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void
on_pw_vnc_initialized(VncConnection *conn, PasswordData *data)
{
    data->auth_succeeded = TRUE;
    g_main_loop_quit(data->dt->loop);
}

static void
on_pw_vnc_auth_failure(VncConnection *conn, const char *msg,
                       PasswordData *data)
{
    data->auth_failed = TRUE;
    g_main_loop_quit(data->dt->loop);
}

static void
on_pw_vnc_error(VncConnection *conn, const char *msg,
                PasswordData *data)
{
    data->auth_failed = TRUE;
    g_main_loop_quit(data->dt->loop);
}

static void
test_dbus_vnc_password_auth(void)
{
    DbusTest dt = { 0 };
    QemuVnc1Server *proxy = NULL;
    g_autoptr(GError) err = NULL;
    PasswordData pdata = { 0 };
    const char *extra_args[] = { "--password", NULL };
    int vnc_fd;
    guint timeout_id;
    gboolean ret;

    if (!setup_dbus_test_full(&dt, extra_args)) {
        goto cleanup;
    }

    proxy = qemu_vnc1_server_proxy_new_sync(
        dt.bus_conn,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.qemu.vnc",
        "/org/qemu/Vnc1/Server",
        NULL, &err);
    g_assert_no_error(err);

    /* Verify auth type is "vnc" when --password is used */
    g_assert_cmpstr(qemu_vnc1_server_get_auth(proxy), ==, "vnc");

    /* Set password via D-Bus — should succeed with --password */
    ret = qemu_vnc1_server_call_set_password_sync(
        proxy, "testpass123",
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(ret);

    /* Connect with the correct password */
    vnc_fd = connect_unix_socket(dt.vnc_sock_path);
    g_assert(vnc_fd >= 0);

    pdata.dt = &dt;
    pdata.password = "testpass123";
    pdata.conn = vnc_connection_new();

    g_signal_connect(pdata.conn, "vnc-error",
                     G_CALLBACK(on_pw_vnc_error), &pdata);
    g_signal_connect(pdata.conn, "vnc-auth-failure",
                     G_CALLBACK(on_pw_vnc_auth_failure), &pdata);
    g_signal_connect(pdata.conn, "vnc-auth-credential",
                     G_CALLBACK(on_pw_vnc_auth_credential), &pdata);
    g_signal_connect(pdata.conn, "vnc-initialized",
                     G_CALLBACK(on_pw_vnc_initialized), &pdata);
    vnc_connection_set_auth_type(pdata.conn, VNC_CONNECTION_AUTH_VNC);
    vnc_connection_open_fd(pdata.conn, vnc_fd);

    dt.loop = g_main_loop_new(NULL, FALSE);
    timeout_id = g_timeout_add_seconds(10, timeout_cb, NULL);
    g_main_loop_run(dt.loop);
    g_source_remove(timeout_id);

    g_assert_true(pdata.auth_succeeded);
    g_assert_false(pdata.auth_failed);

    vnc_connection_shutdown(pdata.conn);
    g_object_unref(pdata.conn);
    g_main_loop_unref(dt.loop);
    dt.loop = NULL;

cleanup:
    g_clear_object(&proxy);
    cleanup_dbus_test(&dt);
}

int
main(int argc, char **argv)
{
    g_log_set_always_fatal(G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);

    if (getenv("GTK_VNC_DEBUG")) {
        vnc_util_set_debug(true);
    }

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/dbus-vnc/basic", test_dbus_vnc_basic);
    qtest_add_func("/dbus-vnc/server-props", test_dbus_vnc_server_props);
    qtest_add_func("/dbus-vnc/client-lifecycle", test_dbus_vnc_client_lifecycle);
    qtest_add_func("/dbus-vnc/no-password", test_dbus_vnc_no_password);
    qtest_add_func("/dbus-vnc/password-auth", test_dbus_vnc_password_auth);

    return g_test_run();
}
