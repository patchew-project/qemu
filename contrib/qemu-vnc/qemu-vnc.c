/*
 * Standalone VNC server connecting to QEMU via D-Bus display interface.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/cutils.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu-version.h"
#include "ui/vnc.h"
#include "crypto/secret.h"
#include "crypto/tlscredsx509.h"
#include "trace.h"
#include "qemu-vnc.h"

const char *qemu_name;
const char *keyboard_layout;

static bool terminate;
static VncDisplay *vd;

static GType
dbus_display_get_proxy_type(GDBusObjectManagerClient *manager,
                            const gchar *object_path,
                            const gchar *interface_name,
                            gpointer user_data)
{
    static const struct {
        const char *iface;
        GType (*get_type)(void);
    } types[] = {
        { "org.qemu.Display1.Clipboard",
          qemu_dbus_display1_clipboard_proxy_get_type },
        { "org.qemu.Display1.Audio",
          qemu_dbus_display1_audio_proxy_get_type },
        { "org.qemu.Display1.Chardev",
          qemu_dbus_display1_chardev_proxy_get_type },
    };

    if (!interface_name) {
        return G_TYPE_DBUS_OBJECT_PROXY;
    }

    for (int i = 0; i < G_N_ELEMENTS(types); i++) {
        if (g_str_equal(interface_name, types[i].iface)) {
            return types[i].get_type();
        }
    }

    return G_TYPE_DBUS_PROXY;
}

static void
on_bus_closed(GDBusConnection *connection,
              gboolean remote_peer_vanished,
              GError *error,
              gpointer user_data)
{
    terminate = true;
    qemu_notify_event();
}

static void
on_owner_vanished(GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
    trace_qemu_vnc_owner_vanished(name);
    error_report("D-Bus peer %s vanished, terminating", name);
    terminate = true;
    qemu_notify_event();
}

typedef struct {
    GDBusConnection *bus;
    const char *bus_name;
    const char * const *chardev_names;
    bool no_vt;
} ManagerSetupData;

static int
compare_console_paths(const void *a, const void *b)
{
    const char *pa = *(const char **)a;
    const char *pb = *(const char **)b;
    return strcmp(pa, pb);
}

static void
on_manager_ready(GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
    ManagerSetupData *data = user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusObjectManager) manager = NULL;
    GList *objects, *l;
    g_autoptr(GPtrArray) console_paths = NULL;
    bool found = false;

    manager = G_DBUS_OBJECT_MANAGER(
        g_dbus_object_manager_client_new_finish(res, &err));
    if (!manager) {
        error_report("Failed to create object manager: %s",
                     err->message);
        terminate = true;
        qemu_notify_event();
        g_free(data);
        return;
    }

    /*
     * Discover all Console objects and sort them so that console
     * indices are assigned in a predictable order matching QEMU's.
     */
    console_paths = g_ptr_array_new_with_free_func(g_free);
    objects = g_dbus_object_manager_get_objects(manager);
    for (l = objects; l; l = l->next) {
        GDBusObject *obj = l->data;
        const char *path = g_dbus_object_get_object_path(obj);

        if (g_str_has_prefix(path, DBUS_DISPLAY1_ROOT "/Console_")) {
            g_ptr_array_add(console_paths, g_strdup(path));
        }
    }
    g_list_free_full(objects, g_object_unref);

    g_ptr_array_sort(console_paths, compare_console_paths);

    for (guint i = 0; i < console_paths->len; i++) {
        const char *path = g_ptr_array_index(console_paths, i);

        if (!console_setup(data->bus, data->bus_name, path)) {
            error_report("Failed to setup console %s", path);
            continue;
        }
        found = true;
    }

    if (!found) {
        error_report("No consoles found");
        terminate = true;
        qemu_notify_event();
        g_free(data);
        return;
    }

    /*
     * Create the VNC display now that consoles exist, so that the
     * display change listener is registered against a valid console.
     */
    {
        Error *local_err = NULL;

        vd = vnc_display_new("default", &local_err);
        if (!vd) {
            error_report_err(local_err);
            terminate = true;
            qemu_notify_event();
            g_free(data);
            return;
        }
    }

    vnc_dbus_setup(data->bus);

    clipboard_setup(manager, data->bus);
    audio_setup(manager);
    if (!data->no_vt) {
        chardev_setup(data->chardev_names, manager);
    }
    g_free(data);
}

int main(int argc, char *argv[])
{
    Error *local_err = NULL;
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusConnection) bus = NULL;
    g_autofree char *dbus_address = NULL;
    g_autofree char *bus_name = NULL;
    int dbus_p2p_fd = -1;
    g_autofree char *vnc_addr = NULL;
    g_autofree char *ws_addr = NULL;
    g_autofree char *share = NULL;
    g_autofree char *tls_creds_dir = NULL;
    g_autofree char *trace_opt = NULL;
    g_auto(GStrv) chardev_names = NULL;
    const char *creds_dir;
    bool has_vnc_password = false;
    bool show_version = false;
    bool no_vt = false;
    bool password = false;
    bool lossy = false;
    bool non_adaptive = false;
    g_autoptr(GOptionContext) context = NULL;
    GOptionEntry entries[] = {
        { "dbus-address", 'a', 0, G_OPTION_ARG_STRING, &dbus_address,
          "D-Bus address to connect to (default: session bus)", "ADDRESS" },
        { "dbus-p2p-fd", 'p', 0, G_OPTION_ARG_INT, &dbus_p2p_fd,
          "D-Bus peer-to-peer socket file descriptor", "FD" },
        { "bus-name", 'n', 0, G_OPTION_ARG_STRING, &bus_name,
          "D-Bus bus name (default: org.qemu)", "NAME" },
       { "vnc-addr", 'l', 0, G_OPTION_ARG_STRING, &vnc_addr,
          "VNC display address (default localhost:0)", "ADDR" },
        { "websocket", 'w', 0, G_OPTION_ARG_STRING, &ws_addr,
          "WebSocket address (e.g. port number or addr:port)", "ADDR" },
        { "share", 's', 0, G_OPTION_ARG_STRING, &share,
          "Display sharing policy "
          "(allow-exclusive|force-shared|ignore)", "POLICY" },
        { "tls-creds", 't', 0, G_OPTION_ARG_STRING, &tls_creds_dir,
          "TLS x509 credentials directory", "DIR" },
        { "vt-chardev", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &chardev_names,
          "Chardev type names to expose as text console (repeatable, "
          "default: serial & hmp)", "NAME" },
        { "no-vt", 'N', 0, G_OPTION_ARG_NONE, &no_vt,
          "Do not expose any chardevs as text consoles", NULL },
        { "keyboard-layout", 'k', 0, G_OPTION_ARG_STRING, &keyboard_layout,
          "Keyboard layout", "LAYOUT" },
        { "trace", 'T', 0, G_OPTION_ARG_STRING, &trace_opt,
          "Trace options (same as QEMU -trace)", "PATTERN" },
        { "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
          "Print version information and exit", NULL },
        { "password", 0, 0, G_OPTION_ARG_NONE, &password,
          "Require password authentication (use D-Bus SetPassword to set)",
          NULL },
        { "lossy", 0, 0, G_OPTION_ARG_NONE, &lossy,
          "Enable lossy compression", NULL },
        { "non-adaptive", 0, 0, G_OPTION_ARG_NONE, &non_adaptive,
          "Disable adaptive encodings", NULL },
        { NULL }
    };

    qemu_init_exec_dir(argv[0]);
    qemu_add_data_dir(get_relocated_path(CONFIG_QEMU_DATADIR));

    module_call_init(MODULE_INIT_TRACE);
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_OPTS);
    qemu_add_opts(&qemu_trace_opts);

    context = g_option_context_new("- standalone VNC server for QEMU");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &err)) {
        error_report("Option parsing failed: %s", err->message);
        return 1;
    }

    if (show_version) {
        printf("qemu-vnc " QEMU_FULL_VERSION "\n");
        return 0;
    }

    if (trace_opt) {
        trace_opt_parse(trace_opt);
        qemu_set_log(LOG_TRACE, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return 1;
        }
    }
    trace_init_file();

    if (qemu_init_main_loop(&local_err)) {
        error_report_err(local_err);
        return 1;
    }

    if (!vnc_addr) {
        vnc_addr = g_strdup("localhost:0");
    }

    if (dbus_p2p_fd >= 0 && dbus_address) {
        error_report("--dbus-p2p-fd and --dbus-address are"
                     " mutually exclusive");
        return 1;
    }

    if (dbus_p2p_fd >= 0) {
        g_autoptr(GSocket) socket = NULL;
        g_autoptr(GSocketConnection) socketc = NULL;

        if (bus_name) {
            error_report("--bus-name is not supported with --dbus-p2p-fd");
            return 1;
        }

        socket = g_socket_new_from_fd(dbus_p2p_fd, &err);
        if (!socket) {
            error_report("Failed to create socket from fd %d: %s",
                         dbus_p2p_fd, err->message);
            return 1;
        }

        socketc = g_socket_connection_factory_create_connection(socket);
        if (!socketc) {
            error_report("Failed to create socket connection");
            return 1;
        }

        bus = g_dbus_connection_new_sync(
            G_IO_STREAM(socketc), NULL,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
            NULL, NULL, &err);
    } else if (dbus_address) {
        GDBusConnectionFlags flags =
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT;
        if (bus_name) {
            flags |= G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION;
        }
        bus = g_dbus_connection_new_for_address_sync(
            dbus_address, flags, NULL, NULL, &err);
    } else {
        bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (!bus_name) {
            bus_name = g_strdup("org.qemu");
        }
    }
    if (!bus) {
        error_report("Failed to connect to D-Bus: %s", err->message);
        return 1;
    }

    {
        g_autoptr(QemuDBusDisplay1VMProxy) vm_proxy = QEMU_DBUS_DISPLAY1_VM_PROXY(
            qemu_dbus_display1_vm_proxy_new_sync(
                bus, G_DBUS_PROXY_FLAGS_NONE, bus_name,
                DBUS_DISPLAY1_ROOT "/VM", NULL, &err));
        if (vm_proxy) {
            qemu_name = g_strdup(qemu_dbus_display1_vm_get_name(
                QEMU_DBUS_DISPLAY1_VM(vm_proxy)));
        }
    }

    /*
     * Set up TLS credentials if requested.  The object must exist
     * before vnc_display_open() which looks it up by ID.
     */
    if (tls_creds_dir) {
        if (!object_new_with_props(TYPE_QCRYPTO_TLS_CREDS_X509,
                                   object_get_objects_root(),
                                   "tlscreds0",
                                   &local_err,
                                   "endpoint", "server",
                                   "dir", tls_creds_dir,
                                   "verify-peer", "no",
                                   NULL)) {
            error_report_err(local_err);
            return 1;
        }
    }

    /*
     * Check for systemd credentials: if a vnc-password credential
     * file exists, create a QCryptoSecret and enable VNC password auth.
     */
    creds_dir = g_getenv("CREDENTIALS_DIRECTORY");
    if (creds_dir) {
        g_autofree char *password_path =
            g_build_filename(creds_dir, "vnc-password", NULL);
        if (g_file_test(password_path, G_FILE_TEST_EXISTS)) {
            if (!object_new_with_props(TYPE_QCRYPTO_SECRET,
                                       object_get_objects_root(),
                                       "vncsecret0",
                                       &local_err,
                                       "file", password_path,
                                       NULL)) {
                error_report_err(local_err);
                return 1;
            }
            has_vnc_password = true;
        }
    }

    {
        g_autoptr(GString) vnc_opts = g_string_new(vnc_addr);
        QemuOptsList *olist = qemu_find_opts("vnc");
        QemuOpts *opts;

        if (tls_creds_dir) {
            g_string_append(vnc_opts, ",tls-creds=tlscreds0");
        }
        if (has_vnc_password) {
            g_string_append(vnc_opts, ",password-secret=vncsecret0");
        }
        if (ws_addr) {
            g_string_append_printf(vnc_opts, ",websocket=%s", ws_addr);
        }
        if (share) {
            g_string_append_printf(vnc_opts, ",share=%s", share);
        }
        if (password && !has_vnc_password) {
            g_string_append(vnc_opts, ",password=on");
        }
        if (lossy) {
            g_string_append(vnc_opts, ",lossy=on");
        }
        if (non_adaptive) {
            g_string_append(vnc_opts, ",non-adaptive=on");
        }

        opts = qemu_opts_parse_noisily(olist, vnc_opts->str, true);
        if (!opts) {
            return 1;
        }
        qemu_opts_set_id(opts, g_strdup("default"));
    }

    {
        ManagerSetupData *mgr_data = g_new0(ManagerSetupData, 1);
        mgr_data->bus = bus;
        mgr_data->bus_name = bus_name;
        mgr_data->chardev_names = (const char * const *)chardev_names;
        mgr_data->no_vt = no_vt;

        g_dbus_object_manager_client_new(
            bus, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
            bus_name, DBUS_DISPLAY1_ROOT,
            dbus_display_get_proxy_type,
            NULL, NULL, NULL,
            on_manager_ready, mgr_data);
    }

    g_signal_connect(bus, "closed", G_CALLBACK(on_bus_closed), NULL);

    if (bus_name) {
        g_bus_watch_name_on_connection(bus, bus_name,
                                       G_BUS_NAME_WATCHER_FLAGS_NONE,
                                       NULL, on_owner_vanished,
                                       NULL, NULL);
    }

    while (!terminate) {
        main_loop_wait(false);
    }

    vnc_dbus_cleanup();
    vnc_display_free(vd);

    return 0;
}
