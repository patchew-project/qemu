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

#include <linux/kvm.h>
#include <linux/psp-sev.h>

#include <sys/ioctl.h>

#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sev_i386.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "migration/blocker.h"
#include "migration/qemu-file.h"
#include "migration/misc.h"

#define DEFAULT_GUEST_POLICY    0x1 /* disable debug */
#define DEFAULT_SEV_DEVICE      "/dev/sev"

static SEVState *sev_state;
static Error *sev_mig_blocker;

static const char *const sev_fw_errlist[] = {
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

#define SEV_FW_MAX_ERROR      ARRAY_SIZE(sev_fw_errlist)
#define RAM_SAVE_FLAG_PAGE_ENCRYPTED_BITMAP       0x400

static int
sev_ioctl(int fd, int cmd, void *data, int *error)
{
    int r;
    struct kvm_sev_cmd input;

    memset(&input, 0x0, sizeof(input));

    input.id = cmd;
    input.sev_fd = fd;
    input.data = (__u64)(unsigned long)data;

    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, &input);

    if (error) {
        *error = input.error;
    }

    return r;
}

static int
sev_platform_ioctl(int fd, int cmd, void *data, int *error)
{
    int r;
    struct sev_issue_cmd arg;

    arg.cmd = cmd;
    arg.data = (unsigned long)data;
    r = ioctl(fd, SEV_ISSUE_CMD, &arg);
    if (error) {
        *error = arg.error;
    }

    return r;
}

static const char *
fw_error_to_str(int code)
{
    if (code < 0 || code >= SEV_FW_MAX_ERROR) {
        return "unknown error";
    }

    return sev_fw_errlist[code];
}

static bool
sev_check_state(SevState state)
{
    assert(sev_state);
    return sev_state->state == state ? true : false;
}

static void
sev_set_guest_state(SevState new_state)
{
    assert(new_state < SEV_STATE__MAX);
    assert(sev_state);

    trace_kvm_sev_change_state(SevState_str(sev_state->state),
                               SevState_str(new_state));
    sev_state->state = new_state;
}

static void
sev_ram_block_added(RAMBlockNotifier *n, void *host, size_t size)
{
    int r;
    struct kvm_enc_region range;
    ram_addr_t offset;
    MemoryRegion *mr;

    /*
     * The RAM device presents a memory region that should be treated
     * as IO region and should not be pinned.
     */
    mr = memory_region_from_host(host, &offset);
    if (mr && memory_region_is_ram_device(mr)) {
        return;
    }

    range.addr = (__u64)(unsigned long)host;
    range.size = size;

    trace_kvm_memcrypt_register_region(host, size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_REG_REGION, &range);
    if (r) {
        error_report("%s: failed to register region (%p+%#zx) error '%s'",
                     __func__, host, size, strerror(errno));
        exit(1);
    }
}

static void
sev_ram_block_removed(RAMBlockNotifier *n, void *host, size_t size)
{
    int r;
    struct kvm_enc_region range;

    range.addr = (__u64)(unsigned long)host;
    range.size = size;

    trace_kvm_memcrypt_unregister_region(host, size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_UNREG_REGION, &range);
    if (r) {
        error_report("%s: failed to unregister region (%p+%#zx)",
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
qsev_guest_set_cbitpos(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);
    uint32_t value;

    visit_type_uint32(v, name, &value, errp);
    sev->cbitpos = value;
}

static void
qsev_guest_set_reduced_phys_bits(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);
    uint32_t value;

    visit_type_uint32(v, name, &value, errp);
    sev->reduced_phys_bits = value;
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
qsev_guest_get_cbitpos(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    uint32_t value;
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    value = sev->cbitpos;
    visit_type_uint32(v, name, &value, errp);
}

static void
qsev_guest_get_reduced_phys_bits(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    uint32_t value;
    QSevGuestInfo *sev = QSEV_GUEST_INFO(obj);

    value = sev->reduced_phys_bits;
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
    object_property_add(obj, "cbitpos", "uint32", qsev_guest_get_cbitpos,
                        qsev_guest_set_cbitpos, NULL, NULL, NULL);
    object_property_add(obj, "reduced-phys-bits", "uint32",
                        qsev_guest_get_reduced_phys_bits,
                        qsev_guest_set_reduced_phys_bits, NULL, NULL, NULL);
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

bool
sev_enabled(void)
{
    return sev_state ? true : false;
}

uint64_t
sev_get_me_mask(void)
{
    return sev_state ? sev_state->me_mask : ~0;
}

uint32_t
sev_get_cbit_position(void)
{
    return sev_state ? sev_state->cbitpos : 0;
}

uint32_t
sev_get_reduced_phys_bits(void)
{
    return sev_state ? sev_state->reduced_phys_bits : 0;
}

SevInfo *
sev_get_info(void)
{
    SevInfo *info;

    info = g_new0(SevInfo, 1);
    info->enabled = sev_state ? true : false;

    if (info->enabled) {
        info->api_major = sev_state->api_major;
        info->api_minor = sev_state->api_minor;
        info->build_id = sev_state->build_id;
        info->policy = sev_state->policy;
        info->state = sev_state->state;
        info->handle = sev_state->handle;
    }

    return info;
}

static int
sev_get_pdh_info(int fd, guchar **pdh, size_t *pdh_len, guchar **cert_chain,
                 size_t *cert_chain_len)
{
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    struct sev_user_data_pdh_cert_export export = {};
    int err, r;

    /* query the certificate length */
    r = sev_platform_ioctl(fd, SEV_PDH_CERT_EXPORT, &export, &err);
    if (r < 0) {
        if (err != SEV_RET_INVALID_LEN) {
            error_report("failed to export PDH cert ret=%d fw_err=%d (%s)",
                         r, err, fw_error_to_str(err));
            return 1;
        }
    }

    pdh_data = g_new(guchar, export.pdh_cert_len);
    cert_chain_data = g_new(guchar, export.cert_chain_len);
    export.pdh_cert_address = (unsigned long)pdh_data;
    export.cert_chain_address = (unsigned long)cert_chain_data;

    r = sev_platform_ioctl(fd, SEV_PDH_CERT_EXPORT, &export, &err);
    if (r < 0) {
        error_report("failed to export PDH cert ret=%d fw_err=%d (%s)",
                     r, err, fw_error_to_str(err));
        goto e_free;
    }

    *pdh = pdh_data;
    *pdh_len = export.pdh_cert_len;
    *cert_chain = cert_chain_data;
    *cert_chain_len = export.cert_chain_len;
    return 0;

e_free:
    g_free(pdh_data);
    g_free(cert_chain_data);
    return 1;
}

SevCapability *
sev_get_capabilities(void)
{
    SevCapability *cap = NULL;
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    size_t pdh_len = 0, cert_chain_len = 0;
    uint32_t ebx;
    int fd;

    fd = open(DEFAULT_SEV_DEVICE, O_RDWR);
    if (fd < 0) {
        error_report("%s: Failed to open %s '%s'", __func__,
                     DEFAULT_SEV_DEVICE, strerror(errno));
        return NULL;
    }

    if (sev_get_pdh_info(fd, &pdh_data, &pdh_len,
                         &cert_chain_data, &cert_chain_len)) {
        goto out;
    }

    cap = g_new0(SevCapability, 1);
    cap->pdh = g_base64_encode(pdh_data, pdh_len);
    cap->cert_chain = g_base64_encode(cert_chain_data, cert_chain_len);

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    cap->cbitpos = ebx & 0x3f;

    /*
     * When SEV feature is enabled, we loose one bit in guest physical
     * addressing.
     */
    cap->reduced_phys_bits = 1;

out:
    g_free(pdh_data);
    g_free(cert_chain_data);
    close(fd);
    return cap;
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
    int fw_error, rc;
    QSevGuestInfo *sev = s->sev_info;
    struct kvm_sev_launch_start *start;
    guchar *session = NULL, *dh_cert = NULL;

    start = g_new0(struct kvm_sev_launch_start, 1);

    start->handle = object_property_get_int(OBJECT(sev), "handle",
                                            &error_abort);
    start->policy = object_property_get_int(OBJECT(sev), "policy",
                                            &error_abort);
    if (sev->session_file) {
        if (sev_read_file_base64(sev->session_file, &session, &sz) < 0) {
            goto out;
        }
        start->session_uaddr = (unsigned long)session;
        start->session_len = sz;
    }

    if (sev->dh_cert_file) {
        if (sev_read_file_base64(sev->dh_cert_file, &dh_cert, &sz) < 0) {
            goto out;
        }
        start->dh_uaddr = (unsigned long)dh_cert;
        start->dh_len = sz;
    }

    trace_kvm_sev_launch_start(start->policy, session, dh_cert);
    rc = sev_ioctl(s->sev_fd, KVM_SEV_LAUNCH_START, start, &fw_error);
    if (rc < 0) {
        error_report("%s: LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto out;
    }

    object_property_set_int(OBJECT(sev), start->handle, "handle",
                            &error_abort);
    sev_set_guest_state(SEV_STATE_LAUNCH_UPDATE);
    s->handle = start->handle;
    s->policy = start->policy;
    ret = 0;

out:
    g_free(start);
    g_free(session);
    g_free(dh_cert);
    return ret;
}

static int
sev_launch_update_data(uint8_t *addr, uint64_t len)
{
    int ret, fw_error;
    struct kvm_sev_launch_update_data update;

    if (!addr || !len) {
        return 1;
    }

    update.uaddr = (__u64)(unsigned long)addr;
    update.len = len;
    trace_kvm_sev_launch_update_data(addr, len);
    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_LAUNCH_UPDATE_DATA,
                    &update, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static void
sev_launch_get_measure(Notifier *notifier, void *unused)
{
    int ret, error;
    guchar *data;
    SEVState *s = sev_state;
    struct kvm_sev_launch_measure *measurement;

    if (!sev_check_state(SEV_STATE_LAUNCH_UPDATE)) {
        return;
    }

    measurement = g_new0(struct kvm_sev_launch_measure, 1);

    /* query the measurement blob length */
    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    measurement, &error);
    if (!measurement->len) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        goto free_measurement;
    }

    data = g_new0(guchar, measurement->len);
    measurement->uaddr = (unsigned long)data;

    /* get the measurement blob */
    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    measurement, &error);
    if (ret) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        goto free_data;
    }

    sev_set_guest_state(SEV_STATE_LAUNCH_SECRET);

    /* encode the measurement value and emit the event */
    s->measurement = g_base64_encode(data, measurement->len);
    trace_kvm_sev_launch_measurement(s->measurement);

free_data:
    g_free(data);
free_measurement:
    g_free(measurement);
}

char *
sev_get_launch_measurement(void)
{
    if (sev_state &&
        sev_state->state >= SEV_STATE_LAUNCH_SECRET) {
        return g_strdup(sev_state->measurement);
    }

    return NULL;
}

static Notifier sev_machine_done_notify = {
    .notify = sev_launch_get_measure,
};

static void
sev_launch_finish(SEVState *s)
{
    int ret, error;
    Error *local_err = NULL;

    trace_kvm_sev_launch_finish();
    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_LAUNCH_FINISH, 0, &error);
    if (ret) {
        error_report("%s: LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
        exit(1);
    }

    sev_set_guest_state(SEV_STATE_RUNNING);

    /* add migration blocker */
    error_setg(&sev_mig_blocker,
               "SEV: Migration is not implemented");
    ret = migrate_add_blocker(sev_mig_blocker, &local_err);
    if (local_err) {
        error_report_err(local_err);
        error_free(sev_mig_blocker);
        exit(1);
    }
}

static int
sev_receive_finish(SEVState *s)
{
    int error, ret = 1;

    trace_kvm_sev_receive_finish();
    ret = sev_ioctl(s->sev_fd, KVM_SEV_RECEIVE_FINISH, 0, &error);
    if (ret) {
        error_report("%s: RECEIVE_FINISH ret=%d fw_error=%d '%s'",
                __func__, ret, error, fw_error_to_str(error));
        goto err;
    }

    sev_set_guest_state(SEV_STATE_RUNNING);
err:
    return ret;
}


static void
sev_vm_state_change(void *opaque, int running, RunState state)
{
    SEVState *s = opaque;

    if (running) {
        if (sev_check_state(SEV_STATE_RECEIVE_UPDATE)) {
            sev_receive_finish(s);
        } else if (!sev_check_state(SEV_STATE_RUNNING)) {
            sev_launch_finish(s);
        }
    }
}

static void
sev_send_finish(void)
{
    int ret, error;

    trace_kvm_sev_send_finish();
    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_SEND_FINISH, 0, &error);
    if (ret) {
        error_report("%s: LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
    }

    sev_set_guest_state(SEV_STATE_RUNNING);
}

static void
sev_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;

    if (migration_has_finished(s) ||
        migration_in_postcopy_after_devices(s) ||
        migration_has_failed(s)) {
        if (sev_check_state(SEV_STATE_SEND_UPDATE)) {
            sev_send_finish();
        }
    }
}

static Notifier sev_migration_state_notify = {
    .notify = sev_migration_state_notifier,
};

void *
sev_guest_init(const char *id)
{
    SEVState *s;
    char *devname;
    int ret, fw_error;
    uint32_t ebx;
    uint32_t host_cbitpos;
    struct sev_user_data_status status = {};

    sev_state = s = g_new0(SEVState, 1);
    s->sev_info = lookup_sev_guest_info(id);
    if (!s->sev_info) {
        error_report("%s: '%s' is not a valid '%s' object",
                     __func__, id, TYPE_QSEV_GUEST_INFO);
        goto err;
    }

    s->state = SEV_STATE_UNINIT;

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    host_cbitpos = ebx & 0x3f;

    s->cbitpos = object_property_get_int(OBJECT(s->sev_info), "cbitpos", NULL);
    if (host_cbitpos != s->cbitpos) {
        error_report("%s: cbitpos check failed, host '%d' requested '%d'",
                     __func__, host_cbitpos, s->cbitpos);
        goto err;
    }

    s->reduced_phys_bits = object_property_get_int(OBJECT(s->sev_info),
                                        "reduced-phys-bits", NULL);
    if (s->reduced_phys_bits < 1) {
        error_report("%s: reduced_phys_bits check failed, it should be >=1,"
                     "' requested '%d'", __func__, s->reduced_phys_bits);
        goto err;
    }

    s->me_mask = ~(1UL << s->cbitpos);

    devname = object_property_get_str(OBJECT(s->sev_info), "sev-device", NULL);
    s->sev_fd = open(devname, O_RDWR);
    if (s->sev_fd < 0) {
        error_report("%s: Failed to open %s '%s'", __func__,
                     devname, strerror(errno));
    }
    g_free(devname);
    if (s->sev_fd < 0) {
        goto err;
    }

    ret = sev_platform_ioctl(s->sev_fd, SEV_PLATFORM_STATUS, &status,
                             &fw_error);
    if (ret) {
        error_report("%s: failed to get platform status ret=%d"
                     "fw_error='%d: %s'", __func__, ret, fw_error,
                     fw_error_to_str(fw_error));
        goto err;
    }
    s->build_id = status.build;
    s->api_major = status.api_major;
    s->api_minor = status.api_minor;

    trace_kvm_sev_init();
    ret = sev_ioctl(s->sev_fd, KVM_SEV_INIT, NULL, &fw_error);
    if (ret) {
        error_report("%s: failed to initialize ret=%d fw_error=%d '%s'",
                     __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    /*
     * The LAUNCH context is used for new guest, if its an incoming guest
     * then RECEIVE context will be created after the connection is established.
     */
    if (!runstate_check(RUN_STATE_INMIGRATE)) {
        ret = sev_launch_start(s);
        if (ret) {
            error_report("%s: failed to create encryption context", __func__);
            goto err;
        }
    }

    ram_block_notifier_add(&sev_ram_notifier);
    qemu_add_machine_init_done_notifier(&sev_machine_done_notify);
    qemu_add_vm_change_state_handler(sev_vm_state_change, s);
    add_migration_state_change_notifier(&sev_migration_state_notify);

    return s;
err:
    g_free(sev_state);
    sev_state = NULL;
    return NULL;
}

int
sev_encrypt_data(void *handle, uint8_t *ptr, uint64_t len)
{
    assert(handle);

    /* if SEV is in update state then encrypt the data else do nothing */
    if (sev_check_state(SEV_STATE_LAUNCH_UPDATE)) {
        return sev_launch_update_data(ptr, len);
    }

    return 0;
}

void sev_set_migrate_info(const char *pdh, const char *plat_cert,
                          const char *amd_cert)
{
    SEVState *s = sev_state;

    s->remote_pdh = g_base64_decode(pdh, &s->remote_pdh_len);
    s->remote_plat_cert = g_base64_decode(plat_cert,
                                          &s->remote_plat_cert_len);
    s->amd_cert = g_base64_decode(amd_cert, &s->amd_cert_len);
}

static int
sev_get_send_session_length(void)
{
    int ret, fw_err = 0;
    struct kvm_sev_send_start *start;

    start = g_new0(struct kvm_sev_send_start, 1);

    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_SEND_START, start, &fw_err);
    if (fw_err != SEV_RET_INVALID_LEN) {
        ret = -1;
        error_report("%s: failed to get session length ret=%d fw_error=%d '%s'",
                     __func__, ret, fw_err, fw_error_to_str(fw_err));
        goto err;
    }

    ret = start->session_len;
err:
    g_free(start);
    return ret;
}

static int
sev_send_start(SEVState *s, QEMUFile *f, uint64_t *bytes_sent)
{
    gsize pdh_len = 0, plat_cert_len;
    int session_len, ret, fw_error;
    struct kvm_sev_send_start *start;
    guchar *pdh = NULL, *plat_cert = NULL, *session = NULL;

    if (!s->remote_pdh || !s->remote_plat_cert) {
        error_report("%s: missing remote PDH or PLAT_CERT", __func__);
        return 1;
    }

    start = g_new0(struct kvm_sev_send_start, 1);

    start->pdh_cert_uaddr = (unsigned long) s->remote_pdh;
    start->pdh_cert_len = s->remote_pdh_len;

    start->plat_cert_uaddr = (unsigned long)s->remote_plat_cert;
    start->plat_cert_len = s->remote_plat_cert_len;

    start->amd_cert_uaddr = (unsigned long)s->amd_cert;
    start->amd_cert_len = s->amd_cert_len;

    /* get the session length */
    session_len = sev_get_send_session_length();
    if (session_len < 0) {
        ret = 1;
        goto err;
    }

    session = g_new0(guchar, session_len);
    start->session_uaddr = (unsigned long)session;
    start->session_len = session_len;

    /* Get our PDH certificate */
    ret = sev_get_pdh_info(s->sev_fd, &pdh, &pdh_len,
                           &plat_cert, &plat_cert_len);
    if (ret) {
        error_report("Failed to get our PDH cert");
        goto err;
    }

    trace_kvm_sev_send_start(start->pdh_cert_uaddr, start->pdh_cert_len,
                             start->plat_cert_uaddr, start->plat_cert_len,
                             start->amd_cert_uaddr, start->amd_cert_len);

    ret = sev_ioctl(s->sev_fd, KVM_SEV_SEND_START, start, &fw_error);
    if (ret < 0) {
        error_report("%s: SEND_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    qemu_put_be32(f, start->policy);
    qemu_put_be32(f, pdh_len);
    qemu_put_buffer(f, (uint8_t *)pdh, pdh_len);
    qemu_put_be32(f, start->session_len);
    qemu_put_buffer(f, (uint8_t *)start->session_uaddr, start->session_len);
    *bytes_sent = 12 + pdh_len + start->session_len;

    sev_set_guest_state(SEV_STATE_SEND_UPDATE);

err:
    g_free(start);
    g_free(pdh);
    g_free(plat_cert);
    return ret;
}

static int
sev_send_get_packet_len(int *fw_err)
{
    int ret;
    struct kvm_sev_send_update_data *update;

    update = g_malloc0(sizeof(*update));
    if (!update) {
        return -1;
    }

    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_SEND_UPDATE_DATA, update, fw_err);
    if (*fw_err != SEV_RET_INVALID_LEN) {
        ret = -1;
        error_report("%s: failed to get session length ret=%d fw_error=%d '%s'",
                    __func__, ret, *fw_err, fw_error_to_str(*fw_err));
        goto err;
    }

    ret = update->hdr_len;

err:
    g_free(update);
    return ret;
}

static int
sev_send_update_data(SEVState *s, QEMUFile *f, uint8_t *ptr, uint32_t size,
                     uint64_t *bytes_sent)
{
    int ret, fw_error;
    guchar *trans;
    struct kvm_sev_send_update_data *update;

    /* If this is first call then query the packet header bytes and allocate
     * the packet buffer.
     */
    if (!s->send_packet_hdr) {
        s->send_packet_hdr_len = sev_send_get_packet_len(&fw_error);
        if (s->send_packet_hdr_len < 1) {
            error_report("%s: SEND_UPDATE fw_error=%d '%s'",
                    __func__, fw_error, fw_error_to_str(fw_error));
            return 1;
        }

        s->send_packet_hdr = g_new(gchar, s->send_packet_hdr_len);
    }

    update = g_new0(struct kvm_sev_send_update_data, 1);

    /* allocate transport buffer */
    trans = g_new(guchar, size);

    update->hdr_uaddr = (unsigned long)s->send_packet_hdr;
    update->hdr_len = s->send_packet_hdr_len;
    update->guest_uaddr = (unsigned long)ptr;
    update->guest_len = size;
    update->trans_uaddr = (unsigned long)trans;
    update->trans_len = size;

    trace_kvm_sev_send_update_data(ptr, trans, size);

    ret = sev_ioctl(s->sev_fd, KVM_SEV_SEND_UPDATE_DATA, update, &fw_error);
    if (ret) {
        error_report("%s: SEND_UPDATE_DATA ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    qemu_put_be32(f, update->hdr_len);
    qemu_put_buffer(f, (uint8_t *)update->hdr_uaddr, update->hdr_len);
    *bytes_sent = 4 + update->hdr_len;

    qemu_put_be32(f, update->trans_len);
    qemu_put_buffer(f, (uint8_t *)update->trans_uaddr, update->trans_len);
    *bytes_sent += (4 + update->trans_len);

err:
    g_free(trans);
    g_free(update);
    return ret;
}

int sev_save_outgoing_page(void *handle, QEMUFile *f, uint8_t *ptr,
                           uint32_t sz, uint64_t *bytes_sent)
{
    SEVState *s = sev_state;

    /*
     * If this is a first buffer then create outgoing encryption context
     * and write our PDH, policy and session data.
     */
    if (!sev_check_state(SEV_STATE_SEND_UPDATE) &&
        sev_send_start(s, f, bytes_sent)) {
        error_report("Failed to create outgoing context");
        return 1;
    }

    return sev_send_update_data(s, f, ptr, sz, bytes_sent);
}

static int
sev_receive_start(QSevGuestInfo *sev, QEMUFile *f)
{
    int ret = 1;
    int fw_error;
    struct kvm_sev_receive_start *start;
    gchar *session = NULL, *pdh_cert = NULL;

    start = g_new0(struct kvm_sev_receive_start, 1);

    /* get SEV guest handle */
    start->handle = object_property_get_int(OBJECT(sev), "handle",
            &error_abort);

    /* get the source policy */
    start->policy = qemu_get_be32(f);

    /* get source PDH key */
    start->pdh_len = qemu_get_be32(f);
    pdh_cert = g_new(gchar, start->pdh_len);
    qemu_get_buffer(f, (uint8_t *)pdh_cert, start->pdh_len);
    start->pdh_uaddr = (unsigned long)pdh_cert;

    /* get source session data */
    start->session_len = qemu_get_be32(f);
    session = g_new(gchar, start->session_len);
    qemu_get_buffer(f, (uint8_t *)session, start->session_len);
    start->session_uaddr = (unsigned long)session;

    trace_kvm_sev_receive_start(start->policy, session, pdh_cert);

    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_RECEIVE_START, start, &fw_error);
    if (ret < 0) {
        error_report("Error RECEIVE_START ret=%d fw_error=%d '%s'",
                ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    object_property_set_int(OBJECT(sev), start->handle, "handle", &error_abort);
    sev_set_guest_state(SEV_STATE_RECEIVE_UPDATE);
err:
    g_free(start);
    g_free(session);
    g_free(pdh_cert);

    return ret;
}

static int sev_receive_update_data(QEMUFile *f, uint8_t *ptr)
{
    int ret = 1, fw_error = 0;
    gchar *hdr = NULL, *trans = NULL;
    struct kvm_sev_receive_update_data *update;

    update = g_new0(struct kvm_sev_receive_update_data, 1);

    /* get packet header */
    update->hdr_len = qemu_get_be32(f);
    hdr = g_new(gchar, update->hdr_len);
    qemu_get_buffer(f, (uint8_t *)hdr, update->hdr_len);
    update->hdr_uaddr = (unsigned long)hdr;

    /* get transport buffer */
    update->trans_len = qemu_get_be32(f);
    trans = g_new(gchar, update->trans_len);
    update->trans_uaddr = (unsigned long)trans;
    qemu_get_buffer(f, (uint8_t *)update->trans_uaddr, update->trans_len);

    update->guest_uaddr = (unsigned long) ptr;
    update->guest_len = update->trans_len;

    trace_kvm_sev_receive_update_data(trans, ptr, update->guest_len,
            hdr, update->hdr_len);

    ret = sev_ioctl(sev_state->sev_fd, KVM_SEV_RECEIVE_UPDATE_DATA,
                    update, &fw_error);
    if (ret) {
        error_report("Error RECEIVE_UPDATE_DATA ret=%d fw_error=%d '%s'",
                ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }
err:
    g_free(trans);
    g_free(update);
    g_free(hdr);
    return ret;
}

int sev_load_incoming_page(void *handle, QEMUFile *f, uint8_t *ptr)
{
    SEVState *s = (SEVState *)handle;

    /* If this is first buffer and SEV is not in recieiving state then
     * use RECEIVE_START command to create a encryption context.
     */
    if (!sev_check_state(SEV_STATE_RECEIVE_UPDATE) &&
        sev_receive_start(s->sev_info, f)) {
        return 1;
    }

    return sev_receive_update_data(f, ptr);
}

#define ALIGN(x, y)  (((x)+(y)-1) & ~((y)-1))

int sev_load_incoming_page_enc_bitmap(void *handle, QEMUFile *f)
{
    void *bmap;
    unsigned long npages;
    unsigned long bmap_size, base_gpa;
    struct kvm_page_enc_bitmap e = {};

    base_gpa = qemu_get_be64(f);
    npages = qemu_get_be64(f);
    bmap_size = qemu_get_be64(f);

    bmap = g_malloc0(bmap_size);
    qemu_get_buffer(f, (uint8_t *)bmap, bmap_size);

    trace_kvm_sev_load_page_enc_bitmap(base_gpa, npages << TARGET_PAGE_BITS);

    e.start_gfn = base_gpa >> TARGET_PAGE_BITS;
    e.num_pages = npages;
    e.enc_bitmap = bmap;
    if (kvm_vm_ioctl(kvm_state, KVM_SET_PAGE_ENC_BITMAP, &e) == -1) {
        error_report("KVM_SET_PAGE_ENC_BITMAP ioctl failed %d", errno);
        g_free(bmap);
        return 1;
    }

    g_free(bmap);

    return 0;
}

int sev_save_outgoing_page_enc_bitmap(void *handle, QEMUFile *f,
                                      unsigned long start, uint64_t length)
{
    uint64_t size;
    struct kvm_page_enc_bitmap e = {};

    if (!length) {
        return 0;
    }

    size = ALIGN((length >> TARGET_PAGE_BITS), /*HOST_LONG_BITS*/ 64) / 8;
    e.enc_bitmap = g_malloc0(size);
    e.start_gfn = start >> TARGET_PAGE_BITS;
    e.num_pages = length >> TARGET_PAGE_BITS;

    trace_kvm_sev_save_page_enc_bitmap(start, length);

    if (kvm_vm_ioctl(kvm_state, KVM_GET_PAGE_ENC_BITMAP, &e) == -1) {
        error_report("%s: KVM_GET_PAGE_ENC_BITMAP ioctl failed %d",
                    __func__, errno);
        g_free(e.enc_bitmap);
        return 1;
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_PAGE_ENCRYPTED_BITMAP);
    qemu_put_be64(f, start);
    qemu_put_be64(f, e.num_pages);
    qemu_put_be64(f, size);
    qemu_put_buffer(f, (uint8_t *)e.enc_bitmap, size);

    g_free(e.enc_bitmap);
    return 0;
}

static void
sev_register_types(void)
{
    type_register_static(&qsev_guest_info);
}

type_init(sev_register_types);
