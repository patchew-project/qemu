/*
 * QEMU SEV support
 *
 * Copyright Advanced Micro Devices 2016-2017
 *
 * Author:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"
#include "sysemu/kvm.h"
#include "sysemu/sev.h"
#include "sysemu/sysemu.h"
#include "trace.h"

#define DEBUG_SEV
#ifdef DEBUG_SEV
#define DPRINTF(fmt, ...) \
    do { fprintf(stdout, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define DEFAULT_SEV_DEVICE      "/dev/sev1"

static MemoryRegionRAMReadWriteOps sev_ops;
static bool sev_allowed;
static int sev_fd;

static void
qsev_guest_finalize(Object *obj)
{
}

static char *
qsev_guest_get_sev_device(Object *obj, Error **errp)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    return g_strdup(sev->sev_device);
}

static void
qsev_guest_set_sev_device(Object *obj, const char *value, Error **errp)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    sev->sev_device = g_strdup(value);
}

static void
qsev_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "sev-device",
                                  qsev_guest_get_sev_device,
                                  qsev_guest_set_sev_device,
                                  NULL);
    object_class_property_set_description(oc, "sev-device",
            "device to use for SEV command", NULL);
}

static QSevGuestInfo *
lookup_sev_guest_info(const char *id)
{
    Object *obj;
    QSevGuestInfo *info;

    obj = object_resolve_path_component(
        object_get_objects_root(), id);
    if (!obj) {
        return NULL;
    }

    info = (QSevGuestInfo *)
            object_dynamic_cast(obj, TYPE_QSEV_GUEST_INFO);
    if (!info) {
        return NULL;
    }

    return info;
}

static void
qsev_guest_init(Object *obj)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    object_property_add_link(obj, "launch", TYPE_QSEV_LAUNCH_INFO,
                             (Object **)&sev->launch_info,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);

    sev->sev_device = g_strdup(DEFAULT_SEV_DEVICE);
}

/* sev guest info */
static const TypeInfo qsev_guest_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QSEV_GUEST_INFO,
    .instance_size = sizeof(QSevGuestInfo),
    .instance_finalize = qsev_guest_finalize,
    .class_size = sizeof(QSevGuestInfoClass),
    .class_init = qsev_guest_class_init,
    .instance_init = qsev_guest_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
qsev_launch_finalize(Object *obj)
{
}

static void
qsev_launch_class_init(ObjectClass *oc, void *data)
{
    /* add launch properties */
}

static void
qsev_launch_init(Object *obj)
{
}

/* guest launch */
static const TypeInfo qsev_launch_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QSEV_LAUNCH_INFO,
    .instance_size = sizeof(QSevLaunchInfo),
    .instance_finalize = qsev_launch_finalize,
    .class_size = sizeof(QSevLaunchInfoClass),
    .class_init = qsev_launch_class_init,
    .instance_init = qsev_launch_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static int
sev_mem_write(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    return 0;
}

static int
sev_mem_read(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    return 0;
}

void *
sev_guest_init(const char *id)
{
    Object *obj;
    SEVState *s;
    char *sev_device_name;

    s = g_malloc0(sizeof(SEVState));
    if (!s) {
        return NULL;
    }

    s->sev_info = lookup_sev_guest_info(id);
    if (!s->sev_info) {
        fprintf(stderr, "'%s' not a valid '%s' object\n",
                id, TYPE_QSEV_GUEST_INFO);
        goto err;
    }

    sev_device_name = object_property_get_str(OBJECT(s->sev_info),
                                              "sev-device", NULL);
    sev_fd = open(sev_device_name, O_RDWR);
    if (sev_fd < 0) {
        fprintf(stderr, "%s:%s\n", sev_device_name, strerror(errno));
        goto err;
    }
    g_free(sev_device_name);

    obj = object_resolve_path_type("", TYPE_QSEV_LAUNCH_INFO, NULL);
    if (obj) {
        object_property_set_link(OBJECT(s->sev_info), obj, "launch",
                &error_abort);
    }

    sev_allowed = true;
    return s;
err:
    g_free(s);
    return NULL;
}

void
sev_set_debug_ops(void *handle, MemoryRegion *mr)
{
    sev_ops.read = sev_mem_read;
    sev_ops.write = sev_mem_write;

    memory_region_set_ram_debug_ops(mr, &sev_ops);
}

bool
sev_enabled(void)
{
    return sev_allowed;
}

static void
sev_policy_register_types(void)
{
    type_register_static(&qsev_guest_info);
    type_register_static(&qsev_launch_info);
}

type_init(sev_policy_register_types);
