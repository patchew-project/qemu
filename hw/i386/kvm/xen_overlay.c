/*
 * QEMU Xen emulation: Shared/overlay pages support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "xen_overlay.h"

#include "sysemu/kvm.h"
#include <linux/kvm.h>

#include "standard-headers/xen/memory.h"

static int xen_overlay_map_page_locked(uint32_t space, uint64_t idx, uint64_t gpa);

#define INVALID_GPA UINT64_MAX
#define INVALID_GFN UINT64_MAX

#define TYPE_XEN_OVERLAY "xenoverlay"
OBJECT_DECLARE_SIMPLE_TYPE(XenOverlayState, XEN_OVERLAY)

#define XEN_PAGE_SHIFT 12
#define XEN_PAGE_SIZE (1ULL << XEN_PAGE_SHIFT)

struct XenOverlayState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion shinfo_mem;
    void *shinfo_ptr;
    uint64_t shinfo_gpa;
};

struct XenOverlayState *xen_overlay_singleton;

static void xen_overlay_realize(DeviceState *dev, Error **errp)
{
    XenOverlayState *s = XEN_OVERLAY(dev);

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen overlay page support is for Xen emulation");
        return;
    }

    memory_region_init_ram(&s->shinfo_mem, OBJECT(dev), "xen:shared_info", XEN_PAGE_SIZE, &error_abort);
    memory_region_set_enabled(&s->shinfo_mem, true);
    s->shinfo_ptr = memory_region_get_ram_ptr(&s->shinfo_mem);
    s->shinfo_gpa = INVALID_GPA;
    memset(s->shinfo_ptr, 0, XEN_PAGE_SIZE);
}

static int xen_overlay_post_load(void *opaque, int version_id)
{
    XenOverlayState *s = opaque;

    if (s->shinfo_gpa != INVALID_GPA) {
        xen_overlay_map_page_locked(XENMAPSPACE_shared_info, 0, s->shinfo_gpa);
    }

    return 0;
}

static bool xen_overlay_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static const VMStateDescription xen_overlay_vmstate = {
    .name = "xen_overlay",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_overlay_is_needed,
    .post_load = xen_overlay_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(shinfo_gpa, XenOverlayState),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_overlay_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xen_overlay_realize;
    dc->vmsd = &xen_overlay_vmstate;
}

static const TypeInfo xen_overlay_info = {
    .name          = TYPE_XEN_OVERLAY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenOverlayState),
    .class_init    = xen_overlay_class_init,
};

void xen_overlay_create(void)
{
    xen_overlay_singleton = XEN_OVERLAY(sysbus_create_simple(TYPE_XEN_OVERLAY, -1, NULL));
}

static void xen_overlay_register_types(void)
{
    type_register_static(&xen_overlay_info);
}

type_init(xen_overlay_register_types)

int xen_overlay_map_page(uint32_t space, uint64_t idx, uint64_t gpa)
{
    int ret;

    qemu_mutex_lock_iothread();
    ret = xen_overlay_map_page_locked(space, idx, gpa);
    qemu_mutex_unlock_iothread();

    return ret;
}

/* KVM is the only existing back end for now. Let's not overengineer it yet. */
static int xen_overlay_set_be_shinfo(uint64_t gfn)
{
    struct kvm_xen_hvm_attr xa = {
        .type = KVM_XEN_ATTR_TYPE_SHARED_INFO,
        .u.shared_info.gfn = gfn,
    };

    return kvm_vm_ioctl(kvm_state, KVM_XEN_HVM_SET_ATTR, &xa);
}

static int xen_overlay_map_page_locked(uint32_t space, uint64_t idx, uint64_t gpa)
{
    MemoryRegion *ovl_page;
    int err;

    if (space != XENMAPSPACE_shared_info || idx != 0)
        return -EINVAL;

    if (!xen_overlay_singleton)
        return -ENOENT;

    ovl_page = &xen_overlay_singleton->shinfo_mem;

    /* Xen allows guests to map the same page as many times as it likes
     * into guest physical frames. We don't, because it would be hard
     * to track and restore them all. One mapping of each page is
     * perfectly sufficient for all known guests... and we've tested
     * that theory on a few now in other implementations. dwmw2. */
    if (memory_region_is_mapped(ovl_page)) {
        if (gpa == INVALID_GPA) {
            /* If removing shinfo page, turn the kernel magic off first */
            if (space == XENMAPSPACE_shared_info) {
                err = xen_overlay_set_be_shinfo(INVALID_GFN);
                if (err)
                    return err;
            }
            memory_region_del_subregion(get_system_memory(), ovl_page);
            goto done;
        } else {
            /* Just move it */
            memory_region_set_address(ovl_page, gpa);
        }
    } else if (gpa != INVALID_GPA) {
        memory_region_add_subregion_overlap(get_system_memory(), gpa, ovl_page, 0);
    }

    xen_overlay_set_be_shinfo(gpa >> XEN_PAGE_SHIFT);
 done:
    xen_overlay_singleton->shinfo_gpa = gpa;
    return 0;
}

void *xen_overlay_page_ptr(uint32_t space, uint64_t idx)
{
    if (space != XENMAPSPACE_shared_info || idx != 0)
        return NULL;

    if (!xen_overlay_singleton)
        return NULL;

    return xen_overlay_singleton->shinfo_ptr;
}
