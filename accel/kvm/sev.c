/*
 * QEMU SEV support
 *
 * Copyright Advanced Micro Devices 2016-2018
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

#define DEFAULT_GUEST_POLICY    0x1 /* disable debug */
#define DEFAULT_SEV_DEVICE      "/dev/sev"

static int sev_fd;

#define SEV_FW_MAX_ERROR      0x17

static SevGuestState current_sev_guest_state = SEV_STATE_UNINIT;

static char sev_state_str[SEV_STATE_MAX][10] = {
    "uninit",
    "lupdate",
    "secret",
    "running",
    "supdate",
    "rupdate",
};

static char sev_fw_errlist[SEV_FW_MAX_ERROR][100] = {
    "",
    "Platform state is invalid",
    "Guest state is invalid",
    "Platform configuration is invalid",
    "Buffer too small",
    "Platform is already owned",
    "Certificate is invalid",
    "Policy is not allowed",
    "Guest is not active",
    "Invalid address",
    "Bad signature",
    "Bad measurement",
    "Asid is already owned",
    "Invalid ASID",
    "WBINVD is required",
    "DF_FLUSH is required",
    "Guest handle is invalid",
    "Invalid command",
    "Guest is active",
    "Hardware error",
    "Hardware unsafe",
    "Feature not supported",
    "Invalid parameter"
};

static int
sev_ioctl(int cmd, void *data, int *error)
{
    int r;
    struct kvm_sev_cmd input;

    memset(&input, 0x0, sizeof(input));

    input.id = cmd;
    input.sev_fd = sev_fd;
    input.data = (__u64)data;

    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, &input);

    if (error) {
        *error = input.error;
    }

    return r;
}

static char *
fw_error_to_str(int code)
{
    if (code > SEV_FW_MAX_ERROR) {
        return NULL;
    }

    return sev_fw_errlist[code];
}

static void
sev_set_guest_state(SevGuestState new_state)
{
    assert(new_state < SEV_STATE_MAX);

    trace_kvm_sev_change_state(sev_state_str[current_sev_guest_state],
                               sev_state_str[new_state]);
    current_sev_guest_state = new_state;
}

static void
sev_ram_block_added(RAMBlockNotifier *n, void *host, size_t size)
{
    int r;
    struct kvm_enc_region range;

    range.addr = (__u64)host;
    range.size = size;

    trace_kvm_memcrypt_register_region(host, size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_REG_REGION, &range);
    if (r) {
        error_report("%s: failed to register region (%p+%#lx)",
                     __func__, host, size);
    }
}

static void
sev_ram_block_removed(RAMBlockNotifier *n, void *host, size_t size)
{
    int r;
    struct kvm_enc_region range;

    range.addr = (__u64)host;
    range.size = size;

    trace_kvm_memcrypt_unregister_region(host, size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_UNREG_REGION, &range);
    if (r) {
        error_report("%s: failed to unregister region (%p+%#lx)",
                     __func__, host, size);
    }
}

static struct RAMBlockNotifier sev_ram_notifier = {
    .ram_block_added = sev_ram_block_added,
    .ram_block_removed = sev_ram_block_removed,
};

static void
qsev_guest_finalize(Object *obj)
{
}

static char *
qsev_guest_get_session_file(Object *obj, Error **errp)
{
    QSevGuestInfo *s = QSEV_GUEST_INFO(obj);

    return s->session_file ? g_strdup(s->session_file) : NULL;
}

static void
qsev_guest_set_session_file(Object *obj, const char *value, Error **errp)
{
    QSevGuestInfo *s = QSEV_GUEST_INFO(obj);

    s->session_file = g_strdup(value);
}

static char *
qsev_guest_get_dh_cert_file(Object *obj, Error **errp)
{
    QSevGuestInfo *s = QSEV_GUEST_INFO(obj);

    return g_strdup(s->dh_cert_file);
}

static void
qsev_guest_set_dh_cert_file(Object *obj, const char *value, Error **errp)
{
    QSevGuestInfo *s = QSEV_GUEST_INFO(obj);

    s->dh_cert_file = g_strdup(value);
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
            "SEV device to use", NULL);
    object_class_property_add_str(oc, "dh-cert-file",
                                  qsev_guest_get_dh_cert_file,
                                  qsev_guest_set_dh_cert_file,
                                  NULL);
    object_class_property_set_description(oc, "dh-cert-file",
            "guest owners DH certificate (encoded with base64)", NULL);
    object_class_property_add_str(oc, "session-file",
                                  qsev_guest_get_session_file,
                                  qsev_guest_set_session_file,
                                  NULL);
    object_class_property_set_description(oc, "session-file",
            "guest owners session parameters (encoded with base64)", NULL);
}

static void
qsev_guest_set_handle(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);
    uint32_t value;

    visit_type_uint32(v, name, &value, errp);
    sev->handle = value;
}

static void
qsev_guest_set_policy(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);
    uint32_t value;

    visit_type_uint32(v, name, &value, errp);
    sev->policy = value;
}

static void
qsev_guest_get_policy(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    uint32_t value;
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    value = sev->policy;
    visit_type_uint32(v, name, &value, errp);
}

static void
qsev_guest_get_handle(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    uint32_t value;
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    value = sev->handle;
    visit_type_uint32(v, name, &value, errp);
}

static void
qsev_guest_init(Object *obj)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    sev->sev_device = g_strdup(DEFAULT_SEV_DEVICE);
    sev->policy = DEFAULT_GUEST_POLICY;
    object_property_add(obj, "policy", "uint32", qsev_guest_get_policy,
                        qsev_guest_set_policy, NULL, NULL, NULL);
    object_property_add(obj, "handle", "uint32", qsev_guest_get_handle,
                        qsev_guest_set_handle, NULL, NULL, NULL);
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

static QSevGuestInfo *
lookup_sev_guest_info(const char *id)
{
    Object *obj;
    QSevGuestInfo *info;

    obj = object_resolve_path_component(object_get_objects_root(), id);
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

static int
sev_read_file_base64(const char *filename, guchar **data, gsize *len)
{
    gsize sz;
    gchar *base64;
    GError *error = NULL;

    if (!g_file_get_contents(filename, &base64, &sz, &error)) {
        error_report("failed to read '%s' (%s)", filename, error->message);
        return -1;
    }

    *data = g_base64_decode(base64, len);
    return 0;
}

static int
sev_launch_start(SEVState *s)
{
    gsize sz;
    int ret = 1;
    int fw_error;
    QSevGuestInfo *sev = s->sev_info;
    struct kvm_sev_launch_start *start;
    guchar *session = NULL, *dh_cert = NULL;

    start = g_malloc0(sizeof(*start));
    if (!start) {
        return 1;
    }

    start->handle = object_property_get_int(OBJECT(sev), "handle",
                                            &error_abort);
    start->policy = object_property_get_int(OBJECT(sev), "policy",
                                            &error_abort);
    if (sev->session_file) {
        if (sev_read_file_base64(sev->session_file, &session, &sz) < 0) {
            return 1;
        }
        start->session_uaddr = (unsigned long)session;
        start->session_len = sz;
    }

    if (sev->dh_cert_file) {
        if (sev_read_file_base64(sev->dh_cert_file, &dh_cert, &sz) < 0) {
            return 1;
        }
        start->dh_uaddr = (unsigned long)dh_cert;
        start->dh_len = sz;
    }

    trace_kvm_sev_launch_start(start->policy, session, dh_cert);
    ret = sev_ioctl(KVM_SEV_LAUNCH_START, start, &fw_error);
    if (ret < 0) {
        error_report("%s: LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        return 1;
    }

    object_property_set_int(OBJECT(sev), start->handle, "handle",
                            &error_abort);
    sev_set_guest_state(SEV_STATE_LUPDATE);

    g_free(start);
    g_free(session);
    g_free(dh_cert);

    return 0;
}

void *
sev_guest_init(const char *id)
{
    SEVState *s;
    char *devname;
    int ret, fw_error;

    s = g_malloc0(sizeof(SEVState));
    if (!s) {
        return NULL;
    }

    s->sev_info = lookup_sev_guest_info(id);
    if (!s->sev_info) {
        error_report("%s: '%s' is not a valid '%s' object",
                     __func__, id, TYPE_QSEV_GUEST_INFO);
        goto err;
    }

    devname = object_property_get_str(OBJECT(s->sev_info), "sev-device", NULL);
    sev_fd = open(devname, O_RDWR);
    if (sev_fd < 0) {
        error_report("%s: Failed to open %s '%s'", __func__,
                     devname, strerror(errno));
        goto err;
    }
    g_free(devname);

    trace_kvm_sev_init();
    ret = sev_ioctl(KVM_SEV_INIT, NULL, &fw_error);
    if (ret) {
        error_report("%s: failed to initialize ret=%d fw_error=%d '%s'",
                     __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    ret = sev_launch_start(s);
    if (ret) {
        error_report("%s: failed to create encryption context", __func__);
        goto err;
    }

    ram_block_notifier_add(&sev_ram_notifier);

    return s;
err:
    g_free(s);
    return NULL;
}

static void
sev_register_types(void)
{
    type_register_static(&qsev_guest_info);
}

type_init(sev_register_types);
