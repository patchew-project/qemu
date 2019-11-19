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
    char *opt_script_data;
    uint32_t pulse_interval;
    uint32_t timeout;
    CharBackend chr_awd_node;
    CharBackend chr_notification_node;
    SocketReadState awd_rs;

    QEMUTimer *pulse_timer;
    QEMUTimer *timeout_timer;
    IOThread *iothread;
    GMainContext *worker_context;
} AwdState;

typedef struct AwdClass {
    ObjectClass parent_class;
} AwdClass;

static int awd_chr_send(AwdState *s,
                        const uint8_t *buf,
                        uint32_t size)
{
    int ret = 0;
    uint32_t len = htonl(size);

    if (!size) {
        return 0;
    }

    ret = qemu_chr_fe_write_all(&s->chr_awd_node, (uint8_t *)&len,
                                sizeof(len));
    if (ret != sizeof(len)) {
        goto err;
    }

    ret = qemu_chr_fe_write_all(&s->chr_awd_node, (uint8_t *)buf,
                                size);
    if (ret != size) {
        goto err;
    }

    return 0;

err:
    return ret < 0 ? ret : -EIO;
}

static int awd_chr_can_read(void *opaque)
{
    return AWD_READ_LEN_MAX;
}

static void awd_node_in(void *opaque, const uint8_t *buf, int size)
{
    AwdState *s = AWD(opaque);
    int ret;

    ret = net_fill_rstate(&s->awd_rs, buf, size);
    if (ret == -1) {
        qemu_chr_fe_set_handlers(&s->chr_awd_node, NULL, NULL, NULL, NULL,
                                 NULL, NULL, true);
        error_report("advanced-watchdog get pulse error");
    }
}

static void awd_send_pulse(void *opaque)
{
    AwdState *s = opaque;
    char buf[] = "advanced-watchdog pulse";

    awd_chr_send(s, (uint8_t *)buf, sizeof(buf));
}

static void awd_regular_pulse(void *opaque)
{
    AwdState *s = opaque;

    awd_send_pulse(s);
    timer_mod(s->pulse_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->pulse_interval);
}

static void awd_timeout(void *opaque)
{
    AwdState *s = opaque;
    int ret = 0;

    ret = qemu_chr_fe_write_all(&s->chr_notification_node,
                                (uint8_t *)s->opt_script_data,
                                strlen(s->opt_script_data));
    if (ret) {
        error_report("advanced-watchdog notification failure");
    }
}

static void awd_timer_init(AwdState *s)
{
    AioContext *ctx = iothread_get_aio_context(s->iothread);

    s->timeout_timer = aio_timer_new(ctx, QEMU_CLOCK_VIRTUAL, SCALE_MS,
                                     awd_timeout, s);

    s->pulse_timer = aio_timer_new(ctx, QEMU_CLOCK_VIRTUAL, SCALE_MS,
                                      awd_regular_pulse, s);

    if (!s->pulse_interval) {
        s->pulse_interval = AWD_PULSE_INTERVAL_DEFAULT;
    }

    if (!s->timeout) {
        s->timeout = AWD_TIMEOUT_DEFAULT;
    }

    timer_mod(s->pulse_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->pulse_interval);
}

static void awd_timer_del(AwdState *s)
{
    if (s->pulse_timer) {
        timer_del(s->pulse_timer);
        timer_free(s->pulse_timer);
        s->pulse_timer = NULL;
    }

    if (s->timeout_timer) {
        timer_del(s->timeout_timer);
        timer_free(s->timeout_timer);
        s->timeout_timer = NULL;
    }
 }

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

static void awd_rs_finalize(SocketReadState *awd_rs)
{
    AwdState *s = container_of(awd_rs, AwdState, awd_rs);

    if (!s->server) {
        char buf[] = "advanced-watchdog reply pulse";

        awd_chr_send(s, (uint8_t *)buf, sizeof(buf));
    }

    timer_mod(s->timeout_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->timeout);

    error_report("advanced-watchdog got message : %s", awd_rs->buf);
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

static void awd_iothread(AwdState *s)
{
    object_ref(OBJECT(s->iothread));
    s->worker_context = iothread_get_g_main_context(s->iothread);

    qemu_chr_fe_set_handlers(&s->chr_awd_node, awd_chr_can_read,
                             awd_node_in, NULL, NULL,
                             s, s->worker_context, true);

    awd_timer_init(s);
}

static int get_opt_script_data(AwdState *s)
{
    FILE *opt_fd;
    long fsize;

    opt_fd = fopen(s->opt_script, "r");
    if (opt_fd == NULL) {
        error_report("advanced-watchdog can't open "
                     "opt_script: %s", s->opt_script);
        return -1;
    }

    fseek(opt_fd, 0, SEEK_END);
    fsize = ftell(opt_fd);
    fseek(opt_fd, 0, SEEK_SET);
    s->opt_script_data = malloc(fsize + 1);

    if (!fread(s->opt_script_data, 1, fsize, opt_fd)) {
        error_report("advanced-watchdog can't read "
                     "opt_script: %s", s->opt_script);
        return -1;
    }

    fclose(opt_fd);

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

    if (get_opt_script_data(s)) {
        error_setg(errp, "advanced-watchdog can't get "
                   "opt script data: %s", s->opt_script);
        return;
    }

    net_socket_rs_init(&s->awd_rs, awd_rs_finalize, false);

    awd_iothread(s);

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

    qemu_chr_fe_deinit(&s->chr_awd_node, false);
    qemu_chr_fe_deinit(&s->chr_notification_node, false);

    if (s->iothread) {
        awd_timer_del(s);
    }

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
