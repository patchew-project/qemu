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
#include "sysemu/runstate.h"
#include "trace.h"
#include "migration/blocker.h"
#include "qom/object.h"

#define TYPE_SEV_GUEST "sev-guest"
OBJECT_DECLARE_SIMPLE_TYPE(SevGuestState, SEV_GUEST)


/**
 * SevGuestState:
 *
 * The SevGuestState object is used for creating and managing a SEV
 * guest.
 *
 * # $QEMU \
 *         -object sev-guest,id=sev0 \
 *         -machine ...,memory-encryption=sev0
 */
struct SevGuestState {
    Object parent_obj;

    /* configuration parameters */
    char *sev_device;
    uint32_t policy;
    char *dh_cert_file;
    char *session_file;
    uint32_t cbitpos;
    uint32_t reduced_phys_bits;

    /* runtime state */
    uint32_t handle;
    uint8_t api_major;
    uint8_t api_minor;
    uint8_t build_id;
    uint64_t me_mask;
    int sev_fd;
    SevState state;
    gchar *measurement;
};

#define DEFAULT_GUEST_POLICY    0x1 /* disable debug */
#define DEFAULT_SEV_DEVICE      "/dev/sev"

static SevGuestState *sev_guest;
static Error *sev_mig_blocker;

static MemoryRegionRAMReadWriteOps sev_ops;

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
sev_check_state(const SevGuestState *sev, SevState state)
{
    assert(sev);
    return sev->state == state ? true : false;
}

static void
sev_set_guest_state(SevGuestState *sev, SevState new_state)
{
    assert(new_state < SEV_STATE__MAX);
    assert(sev);

    trace_kvm_sev_change_state(SevState_str(sev->state),
                               SevState_str(new_state));
    sev->state = new_state;
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
    ram_addr_t offset;
    MemoryRegion *mr;

    /*
     * The RAM device presents a memory region that should be treated
     * as IO region and should not have been pinned.
     */
    mr = memory_region_from_host(host, &offset);
    if (mr && memory_region_is_ram_device(mr)) {
        return;
    }

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
sev_guest_finalize(Object *obj)
{
}

static char *
sev_guest_get_session_file(Object *obj, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    return s->session_file ? g_strdup(s->session_file) : NULL;
}

static void
sev_guest_set_session_file(Object *obj, const char *value, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    s->session_file = g_strdup(value);
}

static char *
sev_guest_get_dh_cert_file(Object *obj, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    return g_strdup(s->dh_cert_file);
}

static void
sev_guest_set_dh_cert_file(Object *obj, const char *value, Error **errp)
{
    SevGuestState *s = SEV_GUEST(obj);

    s->dh_cert_file = g_strdup(value);
}

static char *
sev_guest_get_sev_device(Object *obj, Error **errp)
{
    SevGuestState *sev = SEV_GUEST(obj);

    return g_strdup(sev->sev_device);
}

static void
sev_guest_set_sev_device(Object *obj, const char *value, Error **errp)
{
    SevGuestState *sev = SEV_GUEST(obj);

    sev->sev_device = g_strdup(value);
}

static void
sev_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "sev-device",
                                  sev_guest_get_sev_device,
                                  sev_guest_set_sev_device);
    object_class_property_set_description(oc, "sev-device",
            "SEV device to use");
    object_class_property_add_str(oc, "dh-cert-file",
                                  sev_guest_get_dh_cert_file,
                                  sev_guest_set_dh_cert_file);
    object_class_property_set_description(oc, "dh-cert-file",
            "guest owners DH certificate (encoded with base64)");
    object_class_property_add_str(oc, "session-file",
                                  sev_guest_get_session_file,
                                  sev_guest_set_session_file);
    object_class_property_set_description(oc, "session-file",
            "guest owners session parameters (encoded with base64)");
}

static void
sev_guest_instance_init(Object *obj)
{
    SevGuestState *sev = SEV_GUEST(obj);

    sev->sev_device = g_strdup(DEFAULT_SEV_DEVICE);
    sev->policy = DEFAULT_GUEST_POLICY;
    object_property_add_uint32_ptr(obj, "policy", &sev->policy,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "handle", &sev->handle,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "cbitpos", &sev->cbitpos,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "reduced-phys-bits",
                                   &sev->reduced_phys_bits,
                                   OBJ_PROP_FLAG_READWRITE);
}

/* sev guest info */
static const TypeInfo sev_guest_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_SEV_GUEST,
    .instance_size = sizeof(SevGuestState),
    .instance_finalize = sev_guest_finalize,
    .class_init = sev_guest_class_init,
    .instance_init = sev_guest_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static SevGuestState *
lookup_sev_guest_info(const char *id)
{
    Object *obj;
    SevGuestState *info;

    obj = object_resolve_path_component(object_get_objects_root(), id);
    if (!obj) {
        return NULL;
    }

    info = (SevGuestState *)
            object_dynamic_cast(obj, TYPE_SEV_GUEST);
    if (!info) {
        return NULL;
    }

    return info;
}

bool
sev_enabled(void)
{
    return !!sev_guest;
}

uint64_t
sev_get_me_mask(void)
{
    return sev_guest ? sev_guest->me_mask : ~0;
}

uint32_t
sev_get_cbit_position(void)
{
    return sev_guest ? sev_guest->cbitpos : 0;
}

uint32_t
sev_get_reduced_phys_bits(void)
{
    return sev_guest ? sev_guest->reduced_phys_bits : 0;
}

SevInfo *
sev_get_info(void)
{
    SevInfo *info;

    info = g_new0(SevInfo, 1);
    info->enabled = sev_enabled();

    if (info->enabled) {
        info->api_major = sev_guest->api_major;
        info->api_minor = sev_guest->api_minor;
        info->build_id = sev_guest->build_id;
        info->policy = sev_guest->policy;
        info->state = sev_guest->state;
        info->handle = sev_guest->handle;
    }

    return info;
}

static int
sev_get_pdh_info(int fd, guchar **pdh, size_t *pdh_len, guchar **cert_chain,
                 size_t *cert_chain_len, Error **errp)
{
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    struct sev_user_data_pdh_cert_export export = {};
    int err, r;

    /* query the certificate length */
    r = sev_platform_ioctl(fd, SEV_PDH_CERT_EXPORT, &export, &err);
    if (r < 0) {
        if (err != SEV_RET_INVALID_LEN) {
            error_setg(errp, "failed to export PDH cert ret=%d fw_err=%d (%s)",
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
        error_setg(errp, "failed to export PDH cert ret=%d fw_err=%d (%s)",
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
sev_get_capabilities(Error **errp)
{
    SevCapability *cap = NULL;
    guchar *pdh_data = NULL;
    guchar *cert_chain_data = NULL;
    size_t pdh_len = 0, cert_chain_len = 0;
    uint32_t ebx;
    int fd;

    if (!kvm_enabled()) {
        error_setg(errp, "KVM not enabled");
        return NULL;
    }
    if (kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, NULL) < 0) {
        error_setg(errp, "SEV is not enabled in KVM");
        return NULL;
    }

    fd = open(DEFAULT_SEV_DEVICE, O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to open %s",
                         DEFAULT_SEV_DEVICE);
        return NULL;
    }

    if (sev_get_pdh_info(fd, &pdh_data, &pdh_len,
                         &cert_chain_data, &cert_chain_len, errp)) {
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
        g_error_free(error);
        return -1;
    }

    *data = g_base64_decode(base64, len);
    return 0;
}

static int
sev_launch_start(SevGuestState *sev)
{
    gsize sz;
    int ret = 1;
    int fw_error, rc;
    struct kvm_sev_launch_start *start;
    guchar *session = NULL, *dh_cert = NULL;

    start = g_new0(struct kvm_sev_launch_start, 1);

    start->handle = sev->handle;
    start->policy = sev->policy;
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
    rc = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_START, start, &fw_error);
    if (rc < 0) {
        error_report("%s: LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto out;
    }

    sev_set_guest_state(sev, SEV_STATE_LAUNCH_UPDATE);
    sev->handle = start->handle;
    ret = 0;

out:
    g_free(start);
    g_free(session);
    g_free(dh_cert);
    return ret;
}

static int
sev_launch_update_data(SevGuestState *sev, uint8_t *addr, uint64_t len)
{
    int ret, fw_error;
    struct kvm_sev_launch_update_data update;

    if (!addr || !len) {
        return 1;
    }

    update.uaddr = (__u64)(unsigned long)addr;
    update.len = len;
    trace_kvm_sev_launch_update_data(addr, len);
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_UPDATE_DATA,
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
    SevGuestState *sev = sev_guest;
    int ret, error;
    guchar *data;
    struct kvm_sev_launch_measure *measurement;

    if (!sev_check_state(sev, SEV_STATE_LAUNCH_UPDATE)) {
        return;
    }

    measurement = g_new0(struct kvm_sev_launch_measure, 1);

    /* query the measurement blob length */
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    measurement, &error);
    if (!measurement->len) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        goto free_measurement;
    }

    data = g_new0(guchar, measurement->len);
    measurement->uaddr = (unsigned long)data;

    /* get the measurement blob */
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    measurement, &error);
    if (ret) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        goto free_data;
    }

    sev_set_guest_state(sev, SEV_STATE_LAUNCH_SECRET);

    /* encode the measurement value and emit the event */
    sev->measurement = g_base64_encode(data, measurement->len);
    trace_kvm_sev_launch_measurement(sev->measurement);

free_data:
    g_free(data);
free_measurement:
    g_free(measurement);
}

char *
sev_get_launch_measurement(void)
{
    if (sev_guest &&
        sev_guest->state >= SEV_STATE_LAUNCH_SECRET) {
        return g_strdup(sev_guest->measurement);
    }

    return NULL;
}

static Notifier sev_machine_done_notify = {
    .notify = sev_launch_get_measure,
};

static void
sev_launch_finish(SevGuestState *sev)
{
    int ret, error;
    Error *local_err = NULL;

    trace_kvm_sev_launch_finish();
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_LAUNCH_FINISH, 0, &error);
    if (ret) {
        error_report("%s: LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
        exit(1);
    }

    sev_set_guest_state(sev, SEV_STATE_RUNNING);

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

static void
sev_vm_state_change(void *opaque, int running, RunState state)
{
    SevGuestState *sev = opaque;

    if (running) {
        if (!sev_check_state(sev, SEV_STATE_RUNNING)) {
            sev_launch_finish(sev);
        }
    }
}

static int
sev_dbg_enc_dec(uint8_t *dst, const uint8_t *src, uint32_t len, bool write)
{
    int ret, error;
    struct kvm_sev_dbg dbg;

    dbg.src_uaddr = (unsigned long)src;
    dbg.dst_uaddr = (unsigned long)dst;
    dbg.len = len;

    trace_kvm_sev_debug(write ? "encrypt" : "decrypt", src, dst, len);
    ret = sev_ioctl(sev_guest->sev_fd,
                    write ? KVM_SEV_DBG_ENCRYPT : KVM_SEV_DBG_DECRYPT,
                    &dbg, &error);
    if (ret) {
        error_report("%s (%s) 0x%llx->0x%llx+0x%x ret=%d fw_error=%d '%s'",
                     __func__, write ? "write" : "read", dbg.src_uaddr,
                     dbg.dst_uaddr, dbg.len, ret, error,
                     fw_error_to_str(error));
    }

    return ret;
}

static int
sev_mem_read(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    assert(attrs.debug);

    return sev_dbg_enc_dec(dst, src, len, false);
}

static int
sev_mem_write(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    assert(attrs.debug);

    return sev_dbg_enc_dec(dst, src, len, true);
}

void *
sev_guest_init(const char *id)
{
    SevGuestState *sev;
    char *devname;
    int ret, fw_error;
    uint32_t ebx;
    uint32_t host_cbitpos;
    struct sev_user_data_status status = {};

    ret = ram_block_discard_disable(true);
    if (ret) {
        error_report("%s: cannot disable RAM discard", __func__);
        return NULL;
    }

    sev = lookup_sev_guest_info(id);
    if (!sev) {
        error_report("%s: '%s' is not a valid '%s' object",
                     __func__, id, TYPE_SEV_GUEST);
        goto err;
    }

    sev_guest = sev;
    sev->state = SEV_STATE_UNINIT;

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    host_cbitpos = ebx & 0x3f;

    if (host_cbitpos != sev->cbitpos) {
        error_report("%s: cbitpos check failed, host '%d' requested '%d'",
                     __func__, host_cbitpos, sev->cbitpos);
        goto err;
    }

    if (sev->reduced_phys_bits < 1) {
        error_report("%s: reduced_phys_bits check failed, it should be >=1,"
                     " requested '%d'", __func__, sev->reduced_phys_bits);
        goto err;
    }

    sev->me_mask = ~(1UL << sev->cbitpos);

    devname = object_property_get_str(OBJECT(sev), "sev-device", NULL);
    sev->sev_fd = open(devname, O_RDWR);
    if (sev->sev_fd < 0) {
        error_report("%s: Failed to open %s '%s'", __func__,
                     devname, strerror(errno));
    }
    g_free(devname);
    if (sev->sev_fd < 0) {
        goto err;
    }

    ret = sev_platform_ioctl(sev->sev_fd, SEV_PLATFORM_STATUS, &status,
                             &fw_error);
    if (ret) {
        error_report("%s: failed to get platform status ret=%d "
                     "fw_error='%d: %s'", __func__, ret, fw_error,
                     fw_error_to_str(fw_error));
        goto err;
    }
    sev->build_id = status.build;
    sev->api_major = status.api_major;
    sev->api_minor = status.api_minor;

    trace_kvm_sev_init();
    ret = sev_ioctl(sev->sev_fd, KVM_SEV_INIT, NULL, &fw_error);
    if (ret) {
        error_report("%s: failed to initialize ret=%d fw_error=%d '%s'",
                     __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    ret = sev_launch_start(sev);
    if (ret) {
        error_report("%s: failed to create encryption context", __func__);
        goto err;
    }

    ram_block_notifier_add(&sev_ram_notifier);
    qemu_add_machine_init_done_notifier(&sev_machine_done_notify);
    qemu_add_vm_change_state_handler(sev_vm_state_change, sev);

    return sev;
err:
    sev_guest = NULL;
    ram_block_discard_disable(false);
    return NULL;
}

int
sev_encrypt_data(void *handle, uint8_t *ptr, uint64_t len)
{
    SevGuestState *sev = handle;

    assert(sev);

    /* if SEV is in update state then encrypt the data else do nothing */
    if (sev_check_state(sev, SEV_STATE_LAUNCH_UPDATE)) {
        return sev_launch_update_data(sev, ptr, len);
    }

    return 0;
}

void
sev_set_debug_ops_memory_region(void *handle, MemoryRegion *mr)
{
    SevGuestState *s = handle;

    /* If policy does not allow debug then no need to register ops */
    if (s->policy & SEV_POLICY_NODBG) {
        return;
    }

    sev_ops.read = sev_mem_read;
    sev_ops.write = sev_mem_write;

    memory_region_set_ram_debug_ops(mr, &sev_ops);
}

hwaddr sev_cpu_get_phys_page_attrs_debug(CPUState *cs, vaddr addr,
                                         MemTxAttrs *attrs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    target_ulong pde_addr, pte_addr;
    uint64_t pte;
    int32_t a20_mask;
    uint32_t page_offset;
    int page_size;
    uint64_t me_mask;

    me_mask = sev_get_me_mask();

    *attrs = cpu_get_mem_attrs(env);

    a20_mask = x86_get_a20_mask(env);
    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr & a20_mask;
        page_size = 4096;
    } else if (env->cr[4] & CR4_PAE_MASK) {
        target_ulong pdpe_addr;
        uint64_t pde, pdpe;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            bool la57 = env->cr[4] & CR4_LA57_MASK;
            uint64_t pml5e_addr, pml5e;
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = la57 ? (int64_t)addr >> 56 : (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                return -1;
            }

            if (la57) {
                pml5e_addr = ((env->cr[3] & ~0xfff & me_mask) +
                        (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
                pml5e = ldq_phys_debug(cs, pml5e_addr) & me_mask;
                if (!(pml5e & PG_PRESENT_MASK)) {
                    return -1;
                }
            } else {
                pml5e = env->cr[3] & me_mask;
            }

            pml4e_addr = ((pml5e & PG_ADDRESS_MASK) +
                    (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
            pml4e = ldq_phys_debug(cs, pml4e_addr) & me_mask;
            if (!(pml4e & PG_PRESENT_MASK)) {
                return -1;
            }
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) +
                         (((addr >> 30) & 0x1ff) << 3)) & a20_mask;
            pdpe = ldq_phys_debug(cs, pdpe_addr) & me_mask;
            if (!(pdpe & PG_PRESENT_MASK)) {
                return -1;
            }
            if (pdpe & PG_PSE_MASK) {
                page_size = 1024 * 1024 * 1024;
                pte = pdpe;
                goto out;
            }

        } else
#endif
        {
            pdpe_addr = ((env->cr[3] & ~0x1f & me_mask) +
                         ((addr >> 27) & 0x18)) & a20_mask;
            pdpe = ldq_phys_debug(cs, pdpe_addr) & me_mask;
            if (!(pdpe & PG_PRESENT_MASK)) {
                return -1;
            }
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) +
                    (((addr >> 21) & 0x1ff) << 3)) & a20_mask;
        pde = ldq_phys_debug(cs, pde_addr) & me_mask;
        if (!(pde & PG_PRESENT_MASK)) {
            return -1;
        }
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte = pde;
        } else {
            /* 4 KB page */
            pte_addr = ((pde & PG_ADDRESS_MASK) +
                        (((addr >> 12) & 0x1ff) << 3)) & a20_mask;
            page_size = 4096;
            pte = ldq_phys_debug(cs, pte_addr) & me_mask;
        }
        if (!(pte & PG_PRESENT_MASK)) {
            return -1;
        }
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff & me_mask) +
                    ((addr >> 20) & 0xffc)) & a20_mask;
        pde = x86_ldl_phys(cs, pde_addr) & me_mask;
        if (!(pde & PG_PRESENT_MASK)) {
            return -1;
        }
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            page_size = 4096 * 1024;
        } else {
            /* page directory entry */
            pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & a20_mask;
            pte = ldl_phys_debug(cs, pte_addr) & me_mask;
            if (!(pte & PG_PRESENT_MASK)) {
                return -1;
            }
            page_size = 4096;
        }
        pte = pte & a20_mask;
    }

#ifdef TARGET_X86_64
out:
#endif
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);
    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    return pte | page_offset;
}

MemTxResult sev_address_space_write_rom_debug(AddressSpace *as,
                                              hwaddr addr,
                                              MemTxAttrs attrs,
                                              const void *ptr,
                                              hwaddr len)
{
    /* set debug attrs to indicate memory access is from the debugger */
    attrs.debug = 1;

    /*
     * Invoke address_space_rw debug helper
     */
    return address_space_write_rom_debug(as, addr, attrs, ptr, len);
}

MemTxResult sev_address_space_read_debug(AddressSpace *as, hwaddr addr,
                                         MemTxAttrs attrs, void *ptr,
                                         hwaddr len)
{
    /* set debug attrs to indicate memory access is from the debugger */
    attrs.debug = 1;

    /*
     * Invoke address_space_rw debug helper
     */
    return address_space_read_debug(as, addr, attrs, ptr, len);
}

static const MemoryDebugOps sev_debug_ops = {
    .read = sev_address_space_read_debug,
    .write = sev_address_space_write_rom_debug,
    .pte_mask = sev_get_me_mask,
};

void
sev_set_debug_ops_cpu_state(void *handle, CPUState *cs)
{
    SevGuestState *s = handle;
    CPUClass *cc;

    /* If policy does not allow debug then no need to register ops */
    if (s->policy & SEV_POLICY_NODBG) {
        return;
    }

    cc = CPU_GET_CLASS(cs);

    /*
     * Override guest MMU lookup/page-table-walker with SEV specific
     * callback to handle encrypted memory.
     */
    cc->get_phys_page_attrs_debug = sev_cpu_get_phys_page_attrs_debug;

    address_space_set_debug_ops(&sev_debug_ops);
}

static void
sev_register_types(void)
{
    type_register_static(&sev_guest_info);
}

type_init(sev_register_types);
