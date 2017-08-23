/*
 * Vhost User library
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Authors:
 *  Marc-André Lureau <mlureau@redhat.com>
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libvhost-user-glib.h"

/* glib event loop integration for libvhost-user and misc callbacks */

G_STATIC_ASSERT((int)G_IO_IN == (int)VU_WATCH_IN);
G_STATIC_ASSERT((int)G_IO_OUT == (int)VU_WATCH_OUT);
G_STATIC_ASSERT((int)G_IO_PRI == (int)VU_WATCH_PRI);
G_STATIC_ASSERT((int)G_IO_ERR == (int)VU_WATCH_ERR);
G_STATIC_ASSERT((int)G_IO_HUP == (int)VU_WATCH_HUP);

typedef struct vus_gsrc {
    GSource parent;
    VuDev *dev;
    GPollFD gfd;
} vus_gsrc_t;

static gboolean
vus_gsrc_prepare(GSource *src, gint *timeout)
{
    g_assert(timeout);

    *timeout = -1;
    return FALSE;
}

static gboolean
vus_gsrc_check(GSource *src)
{
    vus_gsrc_t *vus_src = (vus_gsrc_t *)src;

    g_assert(vus_src);

    return vus_src->gfd.revents & vus_src->gfd.events;
}

static gboolean
vus_gsrc_dispatch(GSource *src, GSourceFunc cb, gpointer data)
{
    vus_gsrc_t *vus_src = (vus_gsrc_t *)src;

    g_assert(vus_src);

    ((vu_watch_cb)cb) (vus_src->dev, vus_src->gfd.revents, data);

    return G_SOURCE_CONTINUE;
}

static GSourceFuncs vus_gsrc_funcs = {
    vus_gsrc_prepare,
    vus_gsrc_check,
    vus_gsrc_dispatch,
    NULL
};

static GSource *
vug_source_new(VuDev *dev, int fd, GIOCondition cond,
               vu_watch_cb vu_cb, gpointer data)
{
    GSource *vus_gsrc;
    vus_gsrc_t *vus_src;
    guint id;

    g_assert(dev);
    g_assert(fd >= 0);
    g_assert(vu_cb);

    vus_gsrc = g_source_new(&vus_gsrc_funcs, sizeof(vus_gsrc_t));
    g_source_set_callback(vus_gsrc, (GSourceFunc) vu_cb, data, NULL);
    vus_src = (vus_gsrc_t *)vus_gsrc;
    vus_src->dev = dev;
    vus_src->gfd.fd = fd;
    vus_src->gfd.events = cond;

    g_source_add_poll(vus_gsrc, &vus_src->gfd);
    id = g_source_attach(vus_gsrc, NULL);
    g_assert(id);
    g_source_unref(vus_gsrc);

    return vus_gsrc;
}

static void
set_watch(VuDev *vu_dev, int fd, int vu_evt, vu_watch_cb cb, void *pvt)
{
    GSource *src;
    VugDev *dev;

    g_assert(vu_dev);
    g_assert(fd >= 0);
    g_assert(cb);

    dev = container_of(vu_dev, VugDev, parent);
    src = vug_source_new(vu_dev, fd, vu_evt, cb, pvt);
    g_hash_table_replace(dev->fdmap, GINT_TO_POINTER(fd), src);
}

static void
remove_watch(VuDev *vu_dev, int fd)
{
    VugDev *dev;

    g_assert(vu_dev);
    g_assert(fd >= 0);

    dev = container_of(vu_dev, VugDev, parent);
    g_hash_table_remove(dev->fdmap, GINT_TO_POINTER(fd));
}

void
vug_init(VugDev *dev, int socket, GMainLoop *loop,
         vu_panic_cb panic, const VuDevIface *iface)
{
    g_assert(dev);
    g_assert(loop);
    g_assert(iface);

    vu_init(&dev->parent, socket, panic, set_watch, remove_watch, iface);
    dev->loop = loop;
    dev->fdmap = g_hash_table_new_full(NULL, NULL, NULL,
                                       (GDestroyNotify) g_source_destroy);
}

void
vug_deinit(VugDev *dev)
{
    g_assert(dev);

    g_hash_table_unref(dev->fdmap);
}
