/*
 * QEMU DBus display console
 *
 * Copyright (c) 2021 Marc-André Lureau <marcandre.lureau@redhat.com>
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
#include "qapi/error.h"
#include "ui/input.h"
#include "ui/kbd-state.h"
#include "trace.h"

#include <gio/gunixfdlist.h>

#include "dbus.h"

struct _DBusDisplayConsole {
    GDBusObjectSkeleton parent_instance;
    DisplayChangeListener dcl;

    DBusDisplay *display;
    QemuConsole *con;
    GHashTable *listeners;
    DBusDisplayDisplay1Console *iface;

    DBusDisplayDisplay1Keyboard *iface_kbd;
    QKbdState *kbd;

    DBusDisplayDisplay1Mouse *iface_mouse;
    gboolean last_set;
    guint last_x;
    guint last_y;
};

G_DEFINE_TYPE(DBusDisplayConsole, dbus_display_console, G_TYPE_DBUS_OBJECT_SKELETON)

static void
dbus_display_console_set_size(DBusDisplayConsole *self, uint32_t width, uint32_t height)
{
    g_object_set(self->iface,
                 "width", width,
                 "height", height,
                 NULL);
}

static void
dbus_gfx_switch(DisplayChangeListener *dcl,
                struct DisplaySurface *new_surface)
{
    DBusDisplayConsole *self = container_of(dcl, DBusDisplayConsole, dcl);

    dbus_display_console_set_size(self,
                                  surface_width(new_surface),
                                  surface_height(new_surface));
}

static void
dbus_gfx_update(DisplayChangeListener *dcl,
                int x, int y, int w, int h)
{
}

static void
dbus_gl_scanout_disable(DisplayChangeListener *dcl)
{
}

static void
dbus_gl_scanout_texture(DisplayChangeListener *dcl,
                        uint32_t tex_id,
                        bool backing_y_0_top,
                        uint32_t backing_width,
                        uint32_t backing_height,
                        uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h)
{
    DBusDisplayConsole *self = container_of(dcl, DBusDisplayConsole, dcl);

    dbus_display_console_set_size(self, w, h);
}

static void
dbus_gl_scanout_dmabuf(DisplayChangeListener *dcl,
                       QemuDmaBuf *dmabuf)
{
    DBusDisplayConsole *self = container_of(dcl, DBusDisplayConsole, dcl);

    dbus_display_console_set_size(self,
                                  dmabuf->width,
                                  dmabuf->height);
}

static void
dbus_gl_scanout_update(DisplayChangeListener *dcl,
                       uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h)
{
}

static const DisplayChangeListenerOps dbus_console_dcl_ops = {
    .dpy_name                = "dbus-console",
    .dpy_gfx_switch          = dbus_gfx_switch,
    .dpy_gfx_update          = dbus_gfx_update,
    .dpy_gl_scanout_disable  = dbus_gl_scanout_disable,
    .dpy_gl_scanout_texture  = dbus_gl_scanout_texture,
    .dpy_gl_scanout_dmabuf   = dbus_gl_scanout_dmabuf,
    .dpy_gl_update           = dbus_gl_scanout_update,
};

static void
dbus_display_console_init(DBusDisplayConsole *object)
{
    DBusDisplayConsole *self = DBUS_DISPLAY_CONSOLE(object);

    self->listeners = g_hash_table_new_full(g_str_hash, g_str_equal,
                                            NULL, g_object_unref);
    self->dcl.ops = &dbus_console_dcl_ops;
}

static void
dbus_display_console_dispose(GObject *object)
{
    DBusDisplayConsole *self = DBUS_DISPLAY_CONSOLE(object);

    unregister_displaychangelistener(&self->dcl);
    g_clear_object(&self->iface_kbd);
    g_clear_object(&self->iface);
    g_clear_pointer(&self->listeners, g_hash_table_unref);
    g_clear_pointer(&self->kbd, qkbd_state_free);

    G_OBJECT_CLASS(dbus_display_console_parent_class)->dispose(object);
}

static void
dbus_display_console_class_init(DBusDisplayConsoleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = dbus_display_console_dispose;
}

static void
listener_vanished_cb(DBusDisplayListener *listener)
{
    DBusDisplayConsole *self = dbus_display_listener_get_console(listener);
    const char *name = dbus_display_listener_get_bus_name(listener);

    trace_dbus_listener_vanished(name);

    g_hash_table_remove(self->listeners, name);
    qkbd_state_lift_all_keys(self->kbd);
}

static gboolean
dbus_console_set_ui_info(DBusDisplayConsole *self,
                         GDBusMethodInvocation *invocation,
                         guint16 arg_width_mm,
                         guint16 arg_height_mm,
                         gint arg_xoff,
                         gint arg_yoff,
                         guint arg_width,
                         guint arg_height)
{
    QemuUIInfo info = {
        .width_mm = arg_width_mm,
        .height_mm = arg_height_mm,
        .xoff = arg_xoff,
        .yoff = arg_yoff,
        .width = arg_width,
        .height = arg_height,
    };

    if (!dpy_ui_info_supported(self->con)) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_UNSUPPORTED,
                                              "SetUIInfo is not supported by guest");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    dpy_set_ui_info(self->con, &info);
    dbus_display_display1_console_complete_set_uiinfo(self->iface, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_console_register_listener(DBusDisplayConsole *self,
                               GDBusMethodInvocation *invocation,
                               GUnixFDList *fd_list,
                               GVariant *arg_listener)
{
    const char *sender = g_dbus_method_invocation_get_sender(invocation);
    GDBusConnection *listener_conn;
    g_autoptr(GError) err = NULL;
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketConnection) socket_conn = NULL;
    g_autofree char *guid = g_dbus_generate_guid();
    DBusDisplayListener *listener;
    int fd;

    if (g_hash_table_contains(self->listeners, sender)) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_INVALID,
                                              "`%s` is already registered!",
                                              sender);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(arg_listener), &err);
    if (err) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_FAILED,
                                              "Couldn't get peer fd: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    socket = g_socket_new_from_fd(fd, &err);
    if (err) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_FAILED,
                                              "Couldn't make a socket: %s", err->message);
        close(fd);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
    /* return now: easier for the other end, as it may handle priv dbus synchronously */
    dbus_display_display1_console_complete_register_listener(self->iface, invocation, NULL);

    if (graphic_hw_register_dbus_listener(self->con, fd)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
    socket_conn = g_socket_connection_factory_create_connection(socket);
    listener_conn = g_dbus_connection_new_sync(G_IO_STREAM(socket_conn),
                                               guid,
                                               G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER,
                                               NULL, NULL, &err);
    if (err) {
        error_report("Failed to setup peer connection: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    listener = dbus_display_listener_new(sender, listener_conn, self);
    if (!listener) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    g_hash_table_insert(self->listeners,
                        (gpointer)dbus_display_listener_get_bus_name(listener),
                        listener);
    g_object_connect(listener_conn,
                     "swapped-signal::closed", listener_vanished_cb, listener,
                     NULL);

    trace_dbus_registered_listener(sender);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_kbd_press(DBusDisplayConsole *self,
               GDBusMethodInvocation *invocation,
               guint arg_keycode)
{
    QKeyCode qcode = qemu_input_key_number_to_qcode(arg_keycode);

    trace_dbus_kbd_press(arg_keycode);

    qkbd_state_key_event(self->kbd, qcode, true);

    dbus_display_display1_keyboard_complete_press(self->iface_kbd, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_kbd_release(DBusDisplayConsole *self,
                 GDBusMethodInvocation *invocation,
                 guint arg_keycode)
{
    QKeyCode qcode = qemu_input_key_number_to_qcode(arg_keycode);

    trace_dbus_kbd_release(arg_keycode);

    qkbd_state_key_event(self->kbd, qcode, false);

    dbus_display_display1_keyboard_complete_release(self->iface_kbd, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static void
dbus_kbd_modifiers_changed(DBusDisplayConsole *self,
                           GParamSpec *pspec)
{
    guint modifiers = dbus_display_display1_keyboard_get_modifiers(self->iface_kbd);

    trace_dbus_kbd_modifiers_changed(modifiers);
}

static void
dbus_kbd_qemu_leds_updated(void *data, int ledstate)
{
    DBusDisplayConsole *self = DBUS_DISPLAY_CONSOLE(data);

    // FIXME: what about self->kbd state?
    dbus_display_display1_keyboard_set_modifiers(self->iface_kbd, ledstate);
}

static gboolean
dbus_mouse_set_pos(DBusDisplayConsole *self,
                   GDBusMethodInvocation *invocation,
                   guint x, guint y)
{
    int width, height;

    trace_dbus_mouse_set_pos(x, y);

    width = qemu_console_get_width(self->con, 0);
    height = qemu_console_get_height(self->con, 0);
    if (qemu_input_is_absolute()) {
        if (x >= width || y >= height) {
            g_dbus_method_invocation_return_error(invocation,
                                                  DBUS_DISPLAY_ERROR,
                                                  DBUS_DISPLAY_ERROR_INVALID,
                                                  "Invalid mouse position");
            return DBUS_METHOD_INVOCATION_HANDLED;
        }
        qemu_input_queue_abs(self->con, INPUT_AXIS_X, x, 0, width);
        qemu_input_queue_abs(self->con, INPUT_AXIS_Y, y, 0, height);
        qemu_input_event_sync();
    } else if (self->last_set) {
        qemu_input_queue_rel(self->con, INPUT_AXIS_X, x - self->last_x);
        qemu_input_queue_rel(self->con, INPUT_AXIS_Y, y - self->last_y);
        qemu_input_event_sync();
    }

    self->last_x = x;
    self->last_y = y;
    self->last_set = TRUE;

    dbus_display_display1_mouse_complete_set_abs_position(self->iface_mouse, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_mouse_press(DBusDisplayConsole *self,
                 GDBusMethodInvocation *invocation,
                 guint button)
{
    trace_dbus_mouse_press(button);

    qemu_input_queue_btn(self->con, button, true);
    qemu_input_event_sync();

    dbus_display_display1_mouse_complete_press(self->iface_mouse, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_mouse_release(DBusDisplayConsole *self,
                   GDBusMethodInvocation *invocation,
                   guint button)
{
    trace_dbus_mouse_release(button);

    qemu_input_queue_btn(self->con, button, false);
    qemu_input_event_sync();

    dbus_display_display1_mouse_complete_release(self->iface_mouse, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

int dbus_display_console_get_index(DBusDisplayConsole *self)
{
    return qemu_console_get_index(self->con);
}

DBusDisplayConsole *
dbus_display_console_new(DBusDisplay *display, QemuConsole *con)
{
    g_autofree char *path = NULL;
    g_autofree char *label = NULL;
    char device_addr[256] = "";
    DBusDisplayConsole *self;
    int idx;

    assert(display);
    assert(con);

    label = qemu_console_get_label(con);
    idx = qemu_console_get_index(con);
    path = g_strdup_printf(DBUS_DISPLAY1_ROOT "/Console_%d", idx);
    self = g_object_new(DBUS_DISPLAY_TYPE_CONSOLE,
                        "g-object-path", path,
                        NULL);
    self->display = display;
    self->con = con;
    /* we should handle errors, and skip non graphics? */
    qemu_console_fill_device_address(con, device_addr, sizeof(device_addr), NULL);

    self->iface = dbus_display_display1_console_skeleton_new();
    g_object_set(self->iface,
        "label", label,
        "type", qemu_console_is_graphic(con) ? "Graphic" : "Text",
        "head", qemu_console_get_head(con),
        "width", qemu_console_get_width(con, 0),
        "height", qemu_console_get_height(con, 0),
        "device-address", device_addr,
        NULL);
    g_object_connect(self->iface,
        "swapped-signal::handle-register-listener", dbus_console_register_listener, self,
        "swapped-signal::handle-set-uiinfo", dbus_console_set_ui_info, self,
        NULL);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
        G_DBUS_INTERFACE_SKELETON(self->iface));

    self->kbd = qkbd_state_init(con);
    self->iface_kbd = dbus_display_display1_keyboard_skeleton_new();
    qemu_add_led_event_handler(dbus_kbd_qemu_leds_updated, self);
    g_object_connect(self->iface_kbd,
        "swapped-signal::handle-press", dbus_kbd_press, self,
        "swapped-signal::handle-release", dbus_kbd_release, self,
        "swapped-signal::notify::modifiers", dbus_kbd_modifiers_changed, self,
        NULL);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
        G_DBUS_INTERFACE_SKELETON(self->iface_kbd));

    self->iface_mouse = dbus_display_display1_mouse_skeleton_new();
    g_object_connect(self->iface_mouse,
        "swapped-signal::handle-set-abs-position", dbus_mouse_set_pos, self,
        "swapped-signal::handle-press", dbus_mouse_press, self,
        "swapped-signal::handle-release", dbus_mouse_release, self,
        NULL);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
        G_DBUS_INTERFACE_SKELETON(self->iface_mouse));

    register_displaychangelistener(&self->dcl);
    return self;
}
