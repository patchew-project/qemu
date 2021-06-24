/*
 * Wayland UI -- A simple Qemu UI backend to share buffers with Wayland compositors
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Mostly (boilerplate) based on cgit.freedesktop.org/wayland/weston/tree/clients/simple-dmabuf-egl.c
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "fullscreen-shell-unstable-v1-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

#define MAX_BUFFERS 3

typedef struct wayland_display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct zwp_fullscreen_shell_v1 *fshell;
    struct zwp_linux_dmabuf_v1 *dmabuf;
} wayland_display;

typedef struct wayland_buffer {
    QemuConsole *con;
    QemuDmaBuf *dmabuf;
    struct wl_buffer *buffer;
    bool busy;
} wayland_buffer;

typedef struct wayland_window {
    wayland_display *display;
    DisplayChangeListener dcl;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_callback *callback;
    wayland_buffer buffers[MAX_BUFFERS];
    wayland_buffer *new_buffer;
    bool redraw;
} wayland_window;

static const struct wl_callback_listener frame_listener;

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    xdg_toplevel_handle_configure,
    xdg_toplevel_handle_close,
};

static void wayland_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

static QEMUGLContext wayland_create_context(DisplayChangeListener *dcl,
                                            QEMUGLParams *params)
{
    return NULL;
}

static void wayland_destroy_context(DisplayChangeListener *dcl,
                                    QEMUGLContext ctx)
{
}

static int wayland_make_context_current(DisplayChangeListener *dcl,
                                        QEMUGLContext ctx)
{
    return 0;
}

static void wayland_scanout_disable(DisplayChangeListener *dcl)
{
}

static void wayland_scanout_texture(DisplayChangeListener *dcl,
                                    uint32_t backing_id,
                                    bool backing_y_0_top,
                                    uint32_t backing_width,
                                    uint32_t backing_height,
                                    uint32_t x, uint32_t y,
                                    uint32_t w, uint32_t h)
{
}

static void wayland_release_dmabuf(DisplayChangeListener *dcl,
                                   QemuDmaBuf *dmabuf)
{
}

static void wayland_dispatch_handler(void *opaque)
{
    wayland_display *wldpy = opaque;

    wl_display_prepare_read(wldpy->display);
    wl_display_read_events(wldpy->display);
    wl_display_dispatch_pending(wldpy->display);
    wl_display_flush(wldpy->display);
}

static void wayland_window_redraw(void *data, struct wl_callback *callback,
                                  uint32_t time)
{
    wayland_window *window = data;
    QemuDmaBuf *dmabuf = window->new_buffer->dmabuf;

    if (callback) {
        wl_callback_destroy(callback);
        window->callback = NULL;
    }
    if (!window->redraw) {
        return;
    }
    window->callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(window->callback, &frame_listener, window);

    wl_surface_attach(window->surface, window->new_buffer->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, dmabuf->width, dmabuf->height);
    wl_surface_commit(window->surface);
    wl_display_flush(window->display->display);
    window->redraw = false;
}

static const struct wl_callback_listener frame_listener = {
    wayland_window_redraw
};

static void buffer_release(void *data, struct wl_buffer *buf)
{
    wayland_buffer *buffer = data;
    QemuDmaBuf *dmabuf = buffer->dmabuf;

    dmabuf->fence_fd = -1;
    graphic_hw_gl_block(buffer->con, false);
    graphic_hw_gl_flushed(buffer->con);
    buffer->busy = false;
    wl_buffer_destroy(buf);
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release
};

static wayland_buffer *window_next_buffer(wayland_window *window)
{
    int i;

    for (i = 0; i < MAX_BUFFERS; i++) {
        if (!window->buffers[i].busy) {
            return &window->buffers[i];
        }
    }
    return NULL;
}

static void wayland_scanout_dmabuf(DisplayChangeListener *dcl,
                                   QemuDmaBuf *dmabuf)
{
    wayland_window *window = container_of(dcl, wayland_window, dcl);
    wayland_display *display = window->display;
    wayland_buffer *buffer = window_next_buffer(window);
    struct zwp_linux_buffer_params_v1 *params;

    if (!buffer) {
	error_report("Can't find free buffer\n");
        exit(1);
    }
    params = zwp_linux_dmabuf_v1_create_params(display->dmabuf);
    zwp_linux_buffer_params_v1_add(params, dmabuf->fd, 0, 0, dmabuf->stride,
                                   0, 0);
    buffer->buffer = zwp_linux_buffer_params_v1_create_immed(params,
                                                             dmabuf->width,
                                                             dmabuf->height,
                                                             dmabuf->fourcc,
                                                             0);
    zwp_linux_buffer_params_v1_destroy(params);
    buffer->dmabuf = dmabuf;
    buffer->con = window->dcl.con;
    window->new_buffer = buffer;
    dmabuf->fence_fd = 1;
    wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
}

static void wayland_scanout_flush(DisplayChangeListener *dcl,
                                  uint32_t x, uint32_t y,
                                  uint32_t w, uint32_t h)
{
    wayland_window *window = container_of(dcl, wayland_window, dcl);
    static bool first = true;

    if (!window->new_buffer->busy && !first) {
        graphic_hw_gl_block(window->new_buffer->con, true);
    }

    window->redraw = true;
    if (first || !window->callback) {
        wayland_window_redraw(window, NULL, 0);
    }
    window->new_buffer->busy = true;
    first = false;
}

static const DisplayChangeListenerOps wayland_ops = {
    .dpy_name                = "wayland",
    .dpy_refresh             = wayland_refresh,

    .dpy_gl_ctx_create       = wayland_create_context,
    .dpy_gl_ctx_destroy      = wayland_destroy_context,
    .dpy_gl_ctx_make_current = wayland_make_context_current,

    .dpy_gl_scanout_disable  = wayland_scanout_disable,
    .dpy_gl_scanout_texture  = wayland_scanout_texture,
    .dpy_gl_scanout_dmabuf   = wayland_scanout_dmabuf,
    .dpy_gl_release_dmabuf   = wayland_release_dmabuf,
    .dpy_gl_update           = wayland_scanout_flush,
};

static void early_wayland_init(DisplayOptions *opts)
{
    display_opengl = 1;
}

static void
dmabuf_modifier(void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
		 uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
}

static void
dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
              uint32_t format)
{
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    dmabuf_format,
    dmabuf_modifier
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
    xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    xdg_wm_base_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t id, const char *interface, uint32_t version)
{
    wayland_display *d = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        d->compositor = wl_registry_bind(registry,
			                 id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
	d->wm_base = wl_registry_bind(registry,
				      id, &xdg_wm_base_interface, 1);
	xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
    } else if (strcmp(interface, "zwp_fullscreen_shell_v1") == 0) {
	d->fshell = wl_registry_bind(registry,
	                             id, &zwp_fullscreen_shell_v1_interface,
	                             1);
    } else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
	d->dmabuf = wl_registry_bind(registry,
	                             id, &zwp_linux_dmabuf_v1_interface, 3);
	zwp_linux_dmabuf_v1_add_listener(d->dmabuf, &dmabuf_listener,
	                                 d);
    }
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

static wayland_display *create_display(void)
{
    wayland_display *display;

    display = g_new0(wayland_display, 1);
    display->display = wl_display_connect(NULL);
    assert(display->display);

    display->registry = wl_display_get_registry(display->display);
    wl_registry_add_listener(display->registry,
                             &registry_listener, display);
    wl_display_roundtrip(display->display);
    if (display->dmabuf == NULL) {
	error_report("No zwp_linux_dmabuf global\n");
	exit(1);
    }
    return display;
}

static wayland_window *create_window(wayland_display *display)
{
    wayland_window *window;

    window = g_new0(wayland_window, 1);
    window->display = display;
    window->surface = wl_compositor_create_surface(display->compositor);

    if (display->wm_base) {
        window->xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base,
	                                                  window->surface);
        assert(window->xdg_surface);
        xdg_surface_add_listener(window->xdg_surface,
                                 &xdg_surface_listener, window);
        window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
        assert(window->xdg_toplevel);
        xdg_toplevel_add_listener(window->xdg_toplevel,
                                  &xdg_toplevel_listener, window);
        xdg_toplevel_set_title(window->xdg_toplevel, "qemu-wayland");
        wl_surface_commit(window->surface);
    } else if (display->fshell) {
        zwp_fullscreen_shell_v1_present_surface(display->fshell,
	                                        window->surface,
		                                ZWP_FULLSCREEN_SHELL_V1_PRESENT_METHOD_DEFAULT,
		                                NULL);
    } else {
         assert(0);
    }

    return window;
}

static void wayland_init(DisplayState *ds, DisplayOptions *opts)
{
    QemuConsole *con;
    wayland_display *wldpy;
    wayland_window *wlwdw;
    int idx;

    wldpy = create_display();
    for (idx = 0;; idx++) {
        con = qemu_console_lookup_by_index(idx);
        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }

        wlwdw = create_window(wldpy);
        wlwdw->dcl.con = con;
        wlwdw->dcl.ops = &wayland_ops;
        register_displaychangelistener(&wlwdw->dcl);
    }
    wl_display_roundtrip(wldpy->display);
    qemu_set_fd_handler(wl_display_get_fd(wldpy->display),
                        wayland_dispatch_handler, NULL, wldpy);
}

static QemuDisplay qemu_display_wayland = {
    .type       = DISPLAY_TYPE_WAYLAND,
    .early_init = early_wayland_init,
    .init       = wayland_init,
};

static void register_wayland(void)
{
    qemu_display_register(&qemu_display_wayland);
}

type_init(register_wayland);
