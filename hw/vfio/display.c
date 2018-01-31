/*
 * display support for mdev based vgpu devices
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Authors:
 *    Gerd Hoffmann
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "pci.h"

/* ---------------------------------------------------------------------- */

static void vfio_display_region_update(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    VFIODisplay *dpy = vdev->dpy;
    struct vfio_device_gfx_plane_info plane = {
        .argsz = sizeof(plane),
        .flags = VFIO_GFX_PLANE_TYPE_REGION
    };
    pixman_format_code_t format = PIXMAN_x8r8g8b8;
    int ret;

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &plane);
    if (ret < 0) {
        fprintf(stderr, "ioctl VFIO_DEVICE_QUERY_GFX_PLANE: %s\n",
                strerror(errno));
        return;
    }
    if (!plane.drm_format || !plane.size) {
        return;
    }
    format = qemu_drm_format_to_pixman(plane.drm_format);
    if (!format) {
        return;
    }

    if (dpy->region.buffer.size &&
        dpy->region.buffer.nr != plane.region_index) {
        /* region changed */
        vfio_region_exit(&dpy->region.buffer);
        memset(&dpy->region.buffer, 0, sizeof(dpy->region.buffer));
        dpy->region.surface = NULL;
    }

    if (dpy->region.surface &&
        (surface_width(dpy->region.surface) != plane.width ||
         surface_height(dpy->region.surface) != plane.height ||
         surface_format(dpy->region.surface) != format)) {
        /* size changed */
        dpy->region.surface = NULL;
    }

    if (!dpy->region.buffer.size) {
        /* mmap region */
        ret = vfio_region_setup(OBJECT(vdev), &vdev->vbasedev,
                                &dpy->region.buffer,
                                plane.region_index,
                                "display");
        if (ret != 0) {
            fprintf(stderr, "%s: vfio_region_setup(%d): %s\n",
                    __func__, plane.region_index, strerror(-ret));
            goto err1;
        }
        ret = vfio_region_mmap(&dpy->region.buffer);
        if (ret != 0) {
            fprintf(stderr, "%s: vfio_region_mmap(%d): %s\n", __func__,
                    plane.region_index, strerror(-ret));
            goto err2;
        }
        assert(dpy->region.buffer.mmaps[0].mmap != NULL);
    }

    if (dpy->region.surface == NULL) {
        /* create surface */
        dpy->region.surface = qemu_create_displaysurface_from
            (plane.width, plane.height, format,
             plane.stride, dpy->region.buffer.mmaps[0].mmap);
        dpy_gfx_replace_surface(dpy->con, dpy->region.surface);
    }

    /* full screen update */
    dpy_gfx_update(dpy->con, 0, 0,
                   surface_width(dpy->region.surface),
                   surface_height(dpy->region.surface));
    return;

err2:
    vfio_region_exit(&dpy->region.buffer);
err1:
    memset(&dpy->region.buffer, 0, sizeof(dpy->region.buffer));
}

static const GraphicHwOps vfio_display_region_ops = {
    .gfx_update = vfio_display_region_update,
};

static int vfio_display_region_init(VFIOPCIDevice *vdev, Error **errp)
{
    vdev->dpy = g_new0(VFIODisplay, 1);
    vdev->dpy->con = graphic_console_init(DEVICE(vdev), 0,
                                          &vfio_display_region_ops,
                                          vdev);
    /* TODO: disable hotplug (there is no graphic_console_close) */
    return 0;
}

/* ---------------------------------------------------------------------- */

int vfio_display_probe(VFIOPCIDevice *vdev, Error **errp)
{
    struct vfio_device_gfx_plane_info probe;
    int ret;

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_GFX_PLANE_TYPE_PROBE | VFIO_GFX_PLANE_TYPE_DMABUF;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &probe);
    if (ret == 0) {
        error_setg(errp, "vfio-display: dmabuf support not implemented yet");
        return -1;
    }

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_GFX_PLANE_TYPE_PROBE | VFIO_GFX_PLANE_TYPE_REGION;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &probe);
    if (ret == 0) {
        return vfio_display_region_init(vdev, errp);
    }

    if (vdev->display == ON_OFF_AUTO_AUTO) {
        /* not an error in automatic mode */
        return 0;
    }

    error_setg(errp, "vfio: device doesn't support any (known) display method");
    return -1;
}
