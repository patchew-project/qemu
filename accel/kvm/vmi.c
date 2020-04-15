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
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"

#include "sysemu/vmi-handshake.h"

#define HANDSHAKE_TIMEOUT_SEC 10

typedef struct VMIntrospection {
    Object parent_obj;

    Error *init_error;

    char *chardevid;
    Chardev *chr;
    CharBackend sock;
    int sock_fd;

    qemu_vmi_from_introspector hsk_in;
    uint64_t hsk_in_read_pos;
    uint64_t hsk_in_read_size;
    GSource *hsk_timer;
    uint32_t handshake_timeout;

    int64_t vm_start_time;

    Notifier machine_ready;
    bool created_from_command_line;

    bool kvmi_hooked;
} VMIntrospection;

#define TYPE_VM_INTROSPECTION "introspection"

#define VM_INTROSPECTION(obj) \
    OBJECT_CHECK(VMIntrospection, (obj), TYPE_VM_INTROSPECTION)

static Error *vm_introspection_init(VMIntrospection *i);

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
    VMIntrospection *i = VM_INTROSPECTION(uc);

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
}

static void prop_set_chardev(Object *obj, const char *value, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->chardevid);
    i->chardevid = g_strdup(value);
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

static void class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *uc = USER_CREATABLE_CLASS(oc);

    uc->complete = complete;
}

static void instance_init(Object *obj)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    i->sock_fd = -1;
    i->created_from_command_line = (qdev_hotplug == false);

    update_vm_start_time(i);

    object_property_add_str(obj, "chardev", NULL, prop_set_chardev, NULL);

    i->handshake_timeout = HANDSHAKE_TIMEOUT_SEC;
    object_property_add(obj, "handshake_timeout", "uint32",
                        prop_set_uint32, prop_get_uint32,
                        NULL, &i->handshake_timeout, NULL);
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

static void instance_finalize(Object *obj)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->chardevid);

    cancel_handshake_timer(i);

    if (i->chr) {
        shutdown_socket_fd(i);
        qemu_chr_fe_deinit(&i->sock, true);
        unhook_kvmi(i);
    }

    error_free(i->init_error);
}

static const TypeInfo info = {
    .name              = TYPE_VM_INTROSPECTION,
    .parent            = TYPE_OBJECT,
    .class_init        = class_init,
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

static bool validate_handshake(VMIntrospection *i, Error **errp)
{
    uint32_t min_accepted_size;

    min_accepted_size = offsetof(qemu_vmi_from_introspector, cookie_hash)
                        + QEMU_VMI_COOKIE_HASH_SIZE;

    if (i->hsk_in.struct_size < min_accepted_size) {
        error_setg(errp, "VMI: not enough or invalid handshake data");
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

    cancel_handshake_timer(i);
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
