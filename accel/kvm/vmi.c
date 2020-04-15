/*
 * VM Introspection
 *
 * Copyright (C) 2017-2020 Bitdefender S.R.L.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/kvm.h"
#include "crypto/secret.h"
#include "crypto/hash.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "migration/vmstate.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "qapi/qmp/qobject.h"
#include "monitor/monitor.h"

#include "sysemu/vmi-intercept.h"
#include "sysemu/vmi-handshake.h"

#define HANDSHAKE_TIMEOUT_SEC 10
#define UNHOOK_TIMEOUT_SEC 60

typedef struct VMIntrospection {
    Object parent_obj;

    Error *init_error;

    char *chardevid;
    Chardev *chr;
    CharBackend sock;
    int sock_fd;

    char *keyid;
    Object *key;
    uint8_t cookie_hash[QEMU_VMI_COOKIE_HASH_SIZE];
    bool key_with_cookie;

    qemu_vmi_from_introspector hsk_in;
    uint64_t hsk_in_read_pos;
    uint64_t hsk_in_read_size;
    GSource *hsk_timer;
    uint32_t handshake_timeout;

    int intercepted_action;
    GSource *unhook_timer;
    uint32_t unhook_timeout;
    bool async_unhook;
    bool unhook_on_shutdown;

    int reconnect_time;

    int64_t vm_start_time;

    Notifier machine_ready;
    Notifier migration_state_change;
    bool created_from_command_line;

    void *qmp_monitor;
    QDict *qmp_rsp;

    bool kvmi_hooked;
} VMIntrospection;

typedef struct VMIntrospectionClass {
    ObjectClass parent_class;
    uint32_t instance_counter;
    VMIntrospection *uniq;
} VMIntrospectionClass;

static const char *action_string[] = {
    "none",
    "suspend",
    "resume",
    "force-reset",
    "migrate",
    "shutdown",
};

static bool suspend_pending;
static bool migrate_pending;
static bool shutdown_pending;

#define TYPE_VM_INTROSPECTION "introspection"

#define VM_INTROSPECTION(obj) \
    OBJECT_CHECK(VMIntrospection, (obj), TYPE_VM_INTROSPECTION)
#define VM_INTROSPECTION_CLASS(class) \
    OBJECT_CLASS_CHECK(VMIntrospectionClass, (class), TYPE_VM_INTROSPECTION)

static Error *vm_introspection_init(VMIntrospection *i);
static void vm_introspection_reset(void *opaque);

static void migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;

    if (migration_has_failed(s)) {
        migrate_pending = false;
    }
}

static void machine_ready(Notifier *notifier, void *data)
{
    VMIntrospection *i = container_of(notifier, VMIntrospection, machine_ready);

    i->init_error = vm_introspection_init(i);
    if (i->init_error) {
        Error *err = error_copy(i->init_error);

        error_report_err(err);
        if (i->created_from_command_line) {
            exit(1);
        }
    }
}

static void update_vm_start_time(VMIntrospection *i)
{
    i->vm_start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
}

static void complete(UserCreatable *uc, Error **errp)
{
    VMIntrospectionClass *ic = VM_INTROSPECTION_CLASS(OBJECT(uc)->class);
    VMIntrospection *i = VM_INTROSPECTION(uc);

    if (ic->instance_counter > 1) {
        error_setg(errp, "VMI: only one introspection object can be created");
        return;
    }

    if (!i->chardevid) {
        error_setg(errp, "VMI: chardev is not set");
        return;
    }

    i->machine_ready.notify = machine_ready;
    qemu_add_machine_init_done_notifier(&i->machine_ready);

    /*
     * If the introspection object is created while parsing the command line,
     * the machine_ready callback will be called later. At that time,
     * it vm_introspection_init() fails, exit() will be called.
     *
     * If the introspection object is created through QMP, machine_init_done
     * is already set and qemu_add_machine_init_done_notifier() will
     * call our machine_done() callback. If vm_introspection_init() fails,
     * we don't call exit() and report the error back to the user.
     */
    if (i->init_error) {
        *errp = i->init_error;
        i->init_error = NULL;
        return;
    }

    ic->uniq = i;

    i->migration_state_change.notify = migration_state_notifier;
    add_migration_state_change_notifier(&i->migration_state_change);

    qemu_register_reset(vm_introspection_reset, i);
}

static void prop_set_chardev(Object *obj, const char *value, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->chardevid);
    i->chardevid = g_strdup(value);
}

static void prop_set_key(Object *obj, const char *value, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->keyid);
    i->keyid = g_strdup(value);
}

static bool prop_get_async_unhook(Object *obj, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    return i->async_unhook;
}

static void prop_set_async_unhook(Object *obj, bool value, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    i->async_unhook = value;
}

static bool prop_get_unhook_on_shutdown(Object *obj, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    return i->unhook_on_shutdown;
}

static void prop_set_unhook_on_shutdown(Object *obj, bool value, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    i->unhook_on_shutdown = value;
}

static void prop_get_uint32(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    uint32_t *value = opaque;

    visit_type_uint32(v, name, value, errp);
}

static void prop_set_uint32(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    uint32_t *value = opaque;
    Error *local_err = NULL;

    visit_type_uint32(v, name, value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static bool chardev_is_connected(VMIntrospection *i, Error **errp)
{
    Object *obj = OBJECT(i->chr);

    return obj && object_property_get_bool(obj, "connected", errp);
}

static bool introspection_can_be_deleted(UserCreatable *uc)
{
    VMIntrospection *i = VM_INTROSPECTION(uc);

    return !chardev_is_connected(i, NULL);
}

static void class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *uc = USER_CREATABLE_CLASS(oc);

    uc->complete = complete;
    uc->can_be_deleted = introspection_can_be_deleted;
}

static const VMStateDescription vmstate_introspection = {
    .name = "vm_introspection",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(vm_start_time, VMIntrospection),
        VMSTATE_END_OF_LIST()
    }
};

static void instance_init(Object *obj)
{
    VMIntrospectionClass *ic = VM_INTROSPECTION_CLASS(obj->class);
    VMIntrospection *i = VM_INTROSPECTION(obj);

    ic->instance_counter++;

    i->sock_fd = -1;
    i->created_from_command_line = (qdev_hotplug == false);

    update_vm_start_time(i);

    object_property_add_str(obj, "chardev", NULL, prop_set_chardev, NULL);
    object_property_add_str(obj, "key", NULL, prop_set_key, NULL);

    i->handshake_timeout = HANDSHAKE_TIMEOUT_SEC;
    object_property_add(obj, "handshake_timeout", "uint32",
                        prop_set_uint32, prop_get_uint32,
                        NULL, &i->handshake_timeout, NULL);

    i->unhook_timeout = UNHOOK_TIMEOUT_SEC;
    object_property_add(obj, "unhook_timeout", "uint32",
                        prop_set_uint32, prop_get_uint32,
                        NULL, &i->unhook_timeout, NULL);

    i->async_unhook = true;
    object_property_add_bool(obj, "async_unhook",
                             prop_get_async_unhook,
                             prop_set_async_unhook, NULL);

    i->unhook_on_shutdown = true;
    object_property_add_bool(obj, "unhook_on_shutdown",
                             prop_get_unhook_on_shutdown,
                             prop_set_unhook_on_shutdown, NULL);

    vmstate_register(NULL, 0, &vmstate_introspection, i);
}

static void disconnect_chardev(VMIntrospection *i)
{
    if (chardev_is_connected(i, NULL)) {
        qemu_chr_fe_disconnect(&i->sock);
    }
}

static void unhook_kvmi(VMIntrospection *i)
{
    if (i->kvmi_hooked) {
        if (kvm_vm_ioctl(kvm_state, KVM_INTROSPECTION_UNHOOK, NULL)) {
            error_report("VMI: ioctl/KVM_INTROSPECTION_UNHOOK failed, errno %d",
                         errno);
        }
        i->kvmi_hooked = false;
    }
}

static void shutdown_socket_fd(VMIntrospection *i)
{
    /* signal both ends (kernel, introspector) */
    if (i->sock_fd != -1) {
        shutdown(i->sock_fd, SHUT_RDWR);
        i->sock_fd = -1;
    }
}

static void disconnect_and_unhook_kvmi(VMIntrospection *i)
{
    shutdown_socket_fd(i);
    disconnect_chardev(i);
    unhook_kvmi(i);
}

static void cancel_timer(GSource *timer)
{
    if (timer) {
        g_source_destroy(timer);
        g_source_unref(timer);
    }
}

static void cancel_handshake_timer(VMIntrospection *i)
{
    cancel_timer(i->hsk_timer);
    i->hsk_timer = NULL;
}

static void cancel_unhook_timer(VMIntrospection *i)
{
    cancel_timer(i->unhook_timer);
    i->unhook_timer = NULL;
}

static void instance_finalize(Object *obj)
{
    VMIntrospectionClass *ic = VM_INTROSPECTION_CLASS(obj->class);
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->chardevid);
    g_free(i->keyid);

    cancel_unhook_timer(i);
    cancel_handshake_timer(i);

    if (i->chr) {
        shutdown_socket_fd(i);
        qemu_chr_fe_deinit(&i->sock, true);
        unhook_kvmi(i);
    }

    error_free(i->init_error);

    qobject_unref(i->qmp_rsp);

    ic->instance_counter--;
    if (!ic->instance_counter) {
        ic->uniq = NULL;
    }

    qemu_unregister_reset(vm_introspection_reset, i);
}

static const TypeInfo info = {
    .name              = TYPE_VM_INTROSPECTION,
    .parent            = TYPE_OBJECT,
    .class_init        = class_init,
    .class_size        = sizeof(VMIntrospectionClass),
    .instance_size     = sizeof(VMIntrospection),
    .instance_finalize = instance_finalize,
    .instance_init     = instance_init,
    .interfaces        = (InterfaceInfo[]){
        {TYPE_USER_CREATABLE},
        {}
    }
};

static void register_types(void)
{
    type_register_static(&info);
}

type_init(register_types);

static bool send_handshake_info(VMIntrospection *i, Error **errp)
{
    qemu_vmi_to_introspector send = {};
    const char *vm_name;
    int r;

    send.struct_size = sizeof(send);
    send.start_time = i->vm_start_time;
    memcpy(&send.uuid, &qemu_uuid, sizeof(send.uuid));
    vm_name = qemu_get_vm_name();
    if (vm_name) {
        snprintf(send.name, sizeof(send.name), "%s", vm_name);
        send.name[sizeof(send.name) - 1] = 0;
    }

    r = qemu_chr_fe_write_all(&i->sock, (uint8_t *)&send, sizeof(send));
    if (r != sizeof(send)) {
        error_setg_errno(errp, errno, "VMI: error writing to '%s'",
                         i->chardevid);
        return false;
    }

    /* tcp_chr_write may call tcp_chr_disconnect/CHR_EVENT_CLOSED */
    if (!chardev_is_connected(i, errp)) {
        error_append_hint(errp, "VMI: qemu_chr_fe_write_all() failed");
        return false;
    }

    return true;
}

static bool validate_handshake_cookie(VMIntrospection *i)
{
    if (!i->key_with_cookie) {
        return true;
    }

    return 0 == memcmp(&i->cookie_hash, &i->hsk_in.cookie_hash,
                       sizeof(i->cookie_hash));
}

static bool validate_handshake(VMIntrospection *i, Error **errp)
{
    uint32_t min_accepted_size;

    min_accepted_size = offsetof(qemu_vmi_from_introspector, cookie_hash)
                        + QEMU_VMI_COOKIE_HASH_SIZE;

    if (i->hsk_in.struct_size < min_accepted_size) {
        error_setg(errp, "VMI: not enough or invalid handshake data");
        return false;
    }

    if (!validate_handshake_cookie(i)) {
        error_setg(errp, "VMI: received cookie doesn't match");
        return false;
    }

    /*
     * Check hsk_in.struct_size and sizeof(hsk_in) before accessing any
     * other fields. We might get fewer bytes from applications using
     * old versions if we extended the qemu_vmi_from_introspector structure.
     */

    return true;
}

static bool connect_kernel(VMIntrospection *i, Error **errp)
{
    struct kvm_introspection_feature commands, events;
    struct kvm_introspection_hook kernel;
    const __s32 all_ids = -1;

    memset(&kernel, 0, sizeof(kernel));
    memcpy(kernel.uuid, &qemu_uuid, sizeof(kernel.uuid));
    kernel.fd = i->sock_fd;

    if (kvm_vm_ioctl(kvm_state, KVM_INTROSPECTION_HOOK, &kernel)) {
        error_setg_errno(errp, -errno,
                         "VMI: ioctl/KVM_INTROSPECTION_HOOK failed");
        if (errno == -EPERM) {
            error_append_hint(errp,
                              "Reload the kvm module with kvm.introspection=on");
        }
        return false;
    }

    i->kvmi_hooked = true;

    commands.allow = 1;
    commands.id = all_ids;
    if (kvm_vm_ioctl(kvm_state, KVM_INTROSPECTION_COMMAND, &commands)) {
        error_setg_errno(errp, -errno,
                         "VMI: ioctl/KVM_INTROSPECTION_COMMAND failed");
        unhook_kvmi(i);
        return false;
    }

    events.allow = 1;
    events.id = all_ids;
    if (kvm_vm_ioctl(kvm_state, KVM_INTROSPECTION_EVENT, &events)) {
        error_setg_errno(errp, -errno,
                         "VMI: ioctl/KVM_INTROSPECTION_EVENT failed");
        unhook_kvmi(i);
        return false;
    }

    return true;
}

static void enable_socket_reconnect(VMIntrospection *i)
{
    if (i->sock_fd == -1 && i->reconnect_time) {
        qemu_chr_fe_reconnect_time(&i->sock, i->reconnect_time);
        qemu_chr_fe_disconnect(&i->sock);
        i->reconnect_time = 0;
    }
}

static void maybe_disable_socket_reconnect(VMIntrospection *i)
{
    if (shutdown_pending) {
        /*
         * We've got the shutdown notification, but the guest might not stop.
         * We already caused the introspection tool to unhook
         * because shutdown_pending was set.
         * Let the socket connect again just in case the guest doesn't stop.
         */
        shutdown_pending = false;
        return;
    }

    if (i->reconnect_time == 0) {
        info_report("VMI: disable socket reconnect");
        i->reconnect_time = qemu_chr_fe_reconnect_time(&i->sock, 0);
    }
}

static void continue_with_the_intercepted_action(VMIntrospection *i)
{
    switch (i->intercepted_action) {
    case VMI_INTERCEPT_SUSPEND:
        vm_stop(RUN_STATE_PAUSED);
        break;
    case VMI_INTERCEPT_MIGRATE:
        start_live_migration_thread(migrate_get_current());
        break;
    case VMI_INTERCEPT_SHUTDOWN:
        qemu_system_powerdown_request();
        break;
    default:
        error_report("VMI: %s: unexpected action %d",
                     __func__, i->intercepted_action);
        break;
    }

    info_report("VMI: continue with '%s'",
                action_string[i->intercepted_action]);

    if (i->qmp_rsp) {
        monitor_qmp_respond_later(i->qmp_monitor, i->qmp_rsp);
        i->qmp_monitor = NULL;
        i->qmp_rsp = NULL;
    }
}

/*
 * We should read only the handshake structure,
 * which might have a different size than what we expect.
 */
static int chr_can_read(void *opaque)
{
    VMIntrospection *i = opaque;

    if (i->hsk_timer == NULL || i->sock_fd == -1) {
        return 0;
    }

    /* first, we read the incoming structure size */
    if (i->hsk_in_read_pos == 0) {
        return sizeof(i->hsk_in.struct_size);
    }

    /* validate the incoming structure size */
    if (i->hsk_in.struct_size < sizeof(i->hsk_in.struct_size)) {
        return 0;
    }

    /* read the rest of the incoming structure */
    return i->hsk_in.struct_size - i->hsk_in_read_pos;
}

static bool enough_bytes_for_handshake(VMIntrospection *i)
{
    return i->hsk_in_read_pos  >= sizeof(i->hsk_in.struct_size)
        && i->hsk_in_read_size == i->hsk_in.struct_size;
}

static void validate_and_connect(VMIntrospection *i)
{
    Error *local_err = NULL;

    if (!validate_handshake(i, &local_err) || !connect_kernel(i, &local_err)) {
        error_append_hint(&local_err, "reconnecting\n");
        warn_report_err(local_err);
        disconnect_chardev(i);
    }
}

static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    VMIntrospection *i = opaque;
    size_t to_read;

    i->hsk_in_read_size += size;

    to_read = sizeof(i->hsk_in) - i->hsk_in_read_pos;
    if (to_read > size) {
        to_read = size;
    }

    if (to_read) {
        memcpy((uint8_t *)&i->hsk_in + i->hsk_in_read_pos, buf, to_read);
        i->hsk_in_read_pos += to_read;
    }

    if (enough_bytes_for_handshake(i)) {
        cancel_handshake_timer(i);
        validate_and_connect(i);
    }
}

static gboolean chr_timeout(gpointer opaque)
{
    VMIntrospection *i = opaque;

    warn_report("VMI: the handshake takes too long");

    g_source_unref(i->hsk_timer);
    i->hsk_timer = NULL;

    disconnect_and_unhook_kvmi(i);
    return FALSE;
}

static void chr_event_open(VMIntrospection *i)
{
    Error *local_err = NULL;

    if (suspend_pending || migrate_pending || shutdown_pending) {
        info_report("VMI: %s: too soon (suspend=%d, migrate=%d, shutdown=%d)",
                    __func__, suspend_pending, migrate_pending,
                    shutdown_pending);
        maybe_disable_socket_reconnect(i);
        qemu_chr_fe_disconnect(&i->sock);
        return;
    }

    if (!send_handshake_info(i, &local_err)) {
        error_append_hint(&local_err, "reconnecting\n");
        warn_report_err(local_err);
        disconnect_chardev(i);
        return;
    }

    info_report("VMI: introspection tool connected");

    i->sock_fd = object_property_get_int(OBJECT(i->chr), "fd", NULL);

    memset(&i->hsk_in, 0, sizeof(i->hsk_in));
    i->hsk_in_read_pos = 0;
    i->hsk_in_read_size = 0;
    i->hsk_timer = qemu_chr_timeout_add_ms(i->chr,
                                           i->handshake_timeout * 1000,
                                           chr_timeout, i);
}

static void chr_event_close(VMIntrospection *i)
{
    if (i->sock_fd != -1) {
        warn_report("VMI: introspection tool disconnected");
        disconnect_and_unhook_kvmi(i);
    }

    cancel_unhook_timer(i);
    cancel_handshake_timer(i);

    if (suspend_pending || migrate_pending || shutdown_pending) {
        maybe_disable_socket_reconnect(i);

        if (i->intercepted_action != VMI_INTERCEPT_NONE) {
            continue_with_the_intercepted_action(i);
            i->intercepted_action = VMI_INTERCEPT_NONE;
        }
    }
}

static void chr_event(void *opaque, QEMUChrEvent event)
{
    VMIntrospection *i = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        chr_event_open(i);
        break;
    case CHR_EVENT_CLOSED:
        chr_event_close(i);
        break;
    default:
        break;
    }
}

static gboolean unhook_timeout_cbk(gpointer opaque)
{
    VMIntrospection *i = opaque;

    warn_report("VMI: the introspection tool is too slow");

    g_source_unref(i->unhook_timer);
    i->unhook_timer = NULL;

    disconnect_and_unhook_kvmi(i);
    return FALSE;
}

static VMIntrospection *vm_introspection_object(void)
{
    VMIntrospectionClass *ic;

    ic = VM_INTROSPECTION_CLASS(object_class_by_name(TYPE_VM_INTROSPECTION));

    return ic ? ic->uniq : NULL;
}

bool vm_introspection_qmp_delay(void *mon, QDict *rsp)
{
    VMIntrospection *i = vm_introspection_object();
    bool intercepted;

    intercepted = i && i->intercepted_action == VMI_INTERCEPT_SUSPEND;

    if (intercepted) {
        i->qmp_monitor = mon;
        i->qmp_rsp = rsp;
    }

    return intercepted;
}

/*
 * This ioctl succeeds only when KVM signals the introspection tool.
 * (the socket is connected and the event was sent without error).
 */
static bool signal_introspection_tool_to_unhook(VMIntrospection *i)
{
    int err;

    err = kvm_vm_ioctl(kvm_state, KVM_INTROSPECTION_PREUNHOOK, NULL);

    return !err;
}

static bool record_intercept_action(VMI_intercept_command action)
{
    switch (action) {
    case VMI_INTERCEPT_SUSPEND:
        suspend_pending = true;
        break;
    case VMI_INTERCEPT_RESUME:
        suspend_pending = false;
        break;
    case VMI_INTERCEPT_FORCE_RESET:
        break;
    case VMI_INTERCEPT_MIGRATE:
        migrate_pending = true;
        break;
    case VMI_INTERCEPT_SHUTDOWN:
        shutdown_pending = true;
        break;
    default:
        return false;
    }

    return true;
}

static void wait_until_the_socket_is_closed(VMIntrospection *i)
{
    info_report("VMI: start waiting until fd=%d is closed", i->sock_fd);

    while (i->sock_fd != -1) {
        main_loop_wait(false);
    }

    info_report("VMI: continue with the intercepted action fd=%d", i->sock_fd);

    maybe_disable_socket_reconnect(i);
}

static bool intercept_action(VMIntrospection *i,
                             VMI_intercept_command action, Error **errp)
{
    if (i->intercepted_action != VMI_INTERCEPT_NONE) {
        error_report("VMI: unhook in progress");
        return false;
    }

    switch (action) {
    case VMI_INTERCEPT_SHUTDOWN:
        if (!i->unhook_on_shutdown) {
            return false;
        }
        break;
    case VMI_INTERCEPT_FORCE_RESET:
        disconnect_and_unhook_kvmi(i);
        return false;
    case VMI_INTERCEPT_RESUME:
        enable_socket_reconnect(i);
        return false;
    default:
        break;
    }

    if (!signal_introspection_tool_to_unhook(i)) {
        disconnect_and_unhook_kvmi(i);
        return false;
    }

    i->unhook_timer = qemu_chr_timeout_add_ms(i->chr,
                                              i->unhook_timeout * 1000,
                                              unhook_timeout_cbk, i);

    if (!i->async_unhook) {
        wait_until_the_socket_is_closed(i);
        return false;
    }

    i->intercepted_action = action;
    return true;
}

bool vm_introspection_intercept(VMI_intercept_command action, Error **errp)
{
    VMIntrospection *i = vm_introspection_object();
    bool intercepted = false;

    info_report("VMI: intercept command: %s",
                action < ARRAY_SIZE(action_string)
                ? action_string[action]
                : "unknown");

    if (record_intercept_action(action) && i) {
        intercepted = intercept_action(i, action, errp);
    }

    info_report("VMI: intercept action: %s",
                intercepted ? "delayed" : "continue");

    return intercepted;
}

static void vm_introspection_reset(void *opaque)
{
    VMIntrospection *i = opaque;

    if (i->sock_fd != -1) {
        info_report("VMI: Reset detected. Closing the socket...");
        disconnect_and_unhook_kvmi(i);
    }

    update_vm_start_time(i);

    /* warm reset triggered by user */
    shutdown_pending = false;
}

static bool make_cookie_hash(const char *key_id, uint8_t *cookie_hash,
                             Error **errp)
{
    uint8_t *cookie = NULL, *hash = NULL;
    size_t cookie_size, hash_size = 0;
    bool done = false;

    if (qcrypto_secret_lookup(key_id, &cookie, &cookie_size, errp) == 0
            && qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA1,
                                  (const char *)cookie, cookie_size,
                                  &hash, &hash_size, errp) == 0) {
        if (hash_size == QEMU_VMI_COOKIE_HASH_SIZE) {
            memcpy(cookie_hash, hash, QEMU_VMI_COOKIE_HASH_SIZE);
            done = true;
        } else {
            error_setg(errp, "VMI: hash algorithm size mismatch");
        }
    }

    g_free(cookie);
    g_free(hash);

    return done;
}

static Error *vm_introspection_init(VMIntrospection *i)
{
    Error *err = NULL;
    int kvmi_version;
    Chardev *chr;

    if (!kvm_enabled()) {
        error_setg(&err, "VMI: missing KVM support");
        return err;
    }

    kvmi_version = kvm_check_extension(kvm_state, KVM_CAP_INTROSPECTION);
    if (kvmi_version == 0) {
        error_setg(&err,
                   "VMI: missing kernel built with CONFIG_KVM_INTROSPECTION");
        return err;
    }

    if (i->keyid) {
        if (!make_cookie_hash(i->keyid, i->cookie_hash, &err)) {
            return err;
        }
        i->key_with_cookie = true;
    } else {
        warn_report("VMI: the introspection tool won't be 'authenticated'");
    }

    chr = qemu_chr_find(i->chardevid);
    if (!chr) {
        error_setg(&err, "VMI: device '%s' not found", i->chardevid);
        return err;
    }

    if (!object_property_get_bool(OBJECT(chr), "reconnecting", &err)) {
        error_append_hint(&err, "VMI: missing reconnect=N for '%s'",
                          i->chardevid);
        return err;
    }

    if (!qemu_chr_fe_init(&i->sock, chr, &err)) {
        error_append_hint(&err, "VMI: device '%s' not initialized",
                          i->chardevid);
        return err;
    }

    i->chr = chr;

    qemu_chr_fe_set_handlers(&i->sock, chr_can_read, chr_read, chr_event,
                             NULL, i, NULL, true);

    /*
     * The reconnect timer is triggered by either machine init or by a chardev
     * disconnect. For the QMP creation, when the machine is already started,
     * use an artificial disconnect just to restart the timer.
     */
    if (!i->created_from_command_line) {
        qemu_chr_fe_disconnect(&i->sock);
    }

    return NULL;
}
