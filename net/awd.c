/*
 * Advanced Watch Dog
 *
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Author: Zhang Chen <chen.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "net/net.h"
#include "qom/object_interfaces.h"
#include "qom/object.h"
#include "chardev/char-fe.h"
#include "qemu/sockets.h"
#include "sysemu/iothread.h"

#define TYPE_AWD  "advanced-watchdog"
#define AWD(obj)  OBJECT_CHECK(AwdState, (obj), TYPE_AWD)

#define AWD_READ_LEN_MAX NET_BUFSIZE
/* Default advanced watchdog pulse interval */
#define AWD_PULSE_INTERVAL_DEFAULT 5000
/* Default advanced watchdog timeout */
#define AWD_TIMEOUT_DEFAULT 2000

typedef struct AwdState {
    Object parent;

    bool server;
    char *awd_node;
    char *notification_node;
    char *opt_script;
    uint32_t pulse_interval;
    uint32_t timeout;
    CharBackend chr_awd_node;
    CharBackend chr_notification_node;
    IOThread *iothread;
} AwdState;

typedef struct AwdClass {
    ObjectClass parent_class;
} AwdClass;

static char *awd_get_node(Object *obj, Error **errp)
{
    AwdState *s = AWD(obj);

    return g_strdup(s->awd_node);
}

static void awd_set_node(Object *obj, const char *value, Error **errp)
{
    AwdState *s = AWD(obj);

    g_free(s->awd_node);
    s->awd_node = g_strdup(value);
}

static char *noti_get_node(Object *obj, Error **errp)
{
    AwdState *s = AWD(obj);

    return g_strdup(s->notification_node);
}

static void noti_set_node(Object *obj, const char *value, Error **errp)
{
    AwdState *s = AWD(obj);

    g_free(s->notification_node);
    s->notification_node = g_strdup(value);
}

static char *opt_script_get_node(Object *obj, Error **errp)
{
    AwdState *s = AWD(obj);

    return g_strdup(s->opt_script);
}

static void opt_script_set_node(Object *obj, const char *value, Error **errp)
{
    AwdState *s = AWD(obj);

    g_free(s->opt_script);
    s->opt_script = g_strdup(value);
}

static bool awd_get_server(Object *obj, Error **errp)
{
    AwdState *s = AWD(obj);

    return s->server;
}

static void awd_set_server(Object *obj, bool value, Error **errp)
{
    AwdState *s = AWD(obj);

    s->server = value;
}

static void awd_get_interval(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    AwdState *s = AWD(obj);
    uint32_t value = s->pulse_interval;

    visit_type_uint32(v, name, &value, errp);
}

static void awd_set_interval(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    AwdState *s = AWD(obj);
    Error *local_err = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    if (!value) {
        error_setg(&local_err, "Property '%s.%s' requires a positive value",
                   object_get_typename(obj), name);
        goto out;
    }
    s->pulse_interval = value;

out:
    error_propagate(errp, local_err);
}

static void awd_get_timeout(Object *obj, Visitor *v,
                            const char *name, void *opaque,
                            Error **errp)
{
    AwdState *s = AWD(obj);
    uint32_t value = s->timeout;

    visit_type_uint32(v, name, &value, errp);
}

static void awd_set_timeout(Object *obj, Visitor *v,
                            const char *name, void *opaque,
                            Error **errp)
{
    AwdState *s = AWD(obj);
    Error *local_err = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }

    if (!value) {
        error_setg(&local_err, "Property '%s.%s' requires a positive value",
                   object_get_typename(obj), name);
        goto out;
    }
    s->timeout = value;

out:
    error_propagate(errp, local_err);
}

static int find_and_check_chardev(Chardev **chr,
                                  char *chr_name,
                                  Error **errp)
{
    *chr = qemu_chr_find(chr_name);
    if (*chr == NULL) {
        error_setg(errp, "Device '%s' not found",
                   chr_name);
        return 1;
    }

    if (!qemu_chr_has_feature(*chr, QEMU_CHAR_FEATURE_RECONNECTABLE)) {
        error_setg(errp, "chardev \"%s\" is not reconnectable",
                   chr_name);
        return 1;
    }

    return 0;
}

static void awd_complete(UserCreatable *uc, Error **errp)
{
    AwdState *s = AWD(uc);
    Chardev *chr;

    if (!s->awd_node || !s->iothread ||
        !s->notification_node || !s->opt_script) {
        error_setg(errp, "advanced-watchdog needs 'awd_node', "
                   "'notification_node', 'opt_script' "
                   "and 'server' property set");
        return;
    }

    if (find_and_check_chardev(&chr, s->awd_node, errp) ||
        !qemu_chr_fe_init(&s->chr_awd_node, chr, errp)) {
        error_setg(errp, "advanced-watchdog can't find chardev awd_node: %s",
                   s->awd_node);
        return;
    }

    if (find_and_check_chardev(&chr, s->notification_node, errp) ||
        !qemu_chr_fe_init(&s->chr_notification_node, chr, errp)) {
        error_setg(errp, "advanced-watchdog can't find "
                   "chardev notification_node: %s", s->notification_node);
        return;
    }

    return;
}

static void awd_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = awd_complete;
}

static void awd_init(Object *obj)
{
    AwdState *s = AWD(obj);

    object_property_add_str(obj, "awd_node",
                            awd_get_node, awd_set_node,
                            NULL);

    object_property_add_str(obj, "notification_node",
                            noti_get_node, noti_set_node,
                            NULL);

    object_property_add_str(obj, "opt_script",
                            opt_script_get_node, opt_script_set_node,
                            NULL);

    object_property_add_bool(obj, "server",
                             awd_get_server,
                             awd_set_server, NULL);

    object_property_add(obj, "pulse_interval", "uint32",
                        awd_get_interval,
                        awd_set_interval, NULL, NULL, NULL);

    object_property_add(obj, "timeout", "uint32",
                        awd_get_timeout,
                        awd_set_timeout, NULL, NULL, NULL);

    object_property_add_link(obj, "iothread", TYPE_IOTHREAD,
                            (Object **)&s->iothread,
                            object_property_allow_set_link,
                            OBJ_PROP_LINK_STRONG, NULL);
}

static void awd_finalize(Object *obj)
{
    AwdState *s = AWD(obj);

    g_free(s->awd_node);
    g_free(s->notification_node);
}

static const TypeInfo awd_info = {
    .name = TYPE_AWD,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(AwdState),
    .instance_init = awd_init,
    .instance_finalize = awd_finalize,
    .class_size = sizeof(AwdClass),
    .class_init = awd_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&awd_info);
}

type_init(register_types);
