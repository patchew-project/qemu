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
#include "qemu/uuid.h"
#include "sysemu/kvm.h"
#include "sev_i386.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "migration/blocker.h"
#include "qom/object.h"
#include "monitor/monitor.h"
#include "exec/confidential-guest-support.h"
#include "hw/i386/pc.h"
#include "qemu/range.h"

#define TYPE_SEV_COMMON "sev-common"
OBJECT_DECLARE_SIMPLE_TYPE(SevCommonState, SEV_COMMON)
#define TYPE_SEV_GUEST "sev-guest"
OBJECT_DECLARE_SIMPLE_TYPE(SevGuestState, SEV_GUEST)
#define TYPE_SEV_SNP_GUEST "sev-snp-guest"
OBJECT_DECLARE_SIMPLE_TYPE(SevSnpGuestState, SEV_SNP_GUEST)

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
struct SevCommonState {
    ConfidentialGuestSupport parent_obj;

    /* configuration parameters */
    char *sev_device;
    uint32_t cbitpos;
    uint32_t reduced_phys_bits;

    /* runtime state */
    uint8_t api_major;
    uint8_t api_minor;
    uint8_t build_id;
    uint64_t me_mask;
    int sev_fd;
    SevState state;

    uint32_t reset_cs;
    uint32_t reset_ip;
    bool reset_data_valid;
};

struct SevGuestState {
    SevCommonState sev_common;
    gchar *measurement;

    /* configuration parameters */
    uint32_t handle;
    uint32_t policy;
    char *dh_cert_file;
    char *session_file;
};

struct SevSnpGuestState {
    SevCommonState sev_common;

    /* configuration parameters */
    char *guest_visible_workarounds;
    char *id_block;
    char *id_auth;
    char *host_data;

    struct kvm_snp_init kvm_init_conf;
    struct kvm_sev_snp_launch_start kvm_start_conf;
    struct kvm_sev_snp_launch_finish kvm_finish_conf;
};

#define DEFAULT_GUEST_POLICY    0x1 /* disable debug */
#define DEFAULT_SEV_DEVICE      "/dev/sev"
#define DEFAULT_SEV_SNP_POLICY  0x30000

#define SEV_INFO_BLOCK_GUID     "00f771de-1a7e-4fcb-890e-68c77e2fb44e"
typedef struct __attribute__((__packed__)) SevInfoBlock {
    /* SEV-ES Reset Vector Address */
    uint32_t reset_addr;
} SevInfoBlock;

#define SEV_SNP_BOOT_BLOCK_GUID "bd39c0c2-2f8e-4243-83e8-1b74cebcb7d9"
typedef struct __attribute__((__packed__)) SevSnpBootInfoBlock {
    /* Prevalidate range address */
    uint32_t pre_validated_start;
    uint32_t pre_validated_end;
    /* Secrets page address */
    uint32_t secrets_addr;
    uint32_t secrets_len;
    /* CPUID page address */
    uint32_t cpuid_addr;
    uint32_t cpuid_len;
} SevSnpBootInfoBlock;

static Error *sev_mig_blocker;

static const char *const sev_fw_errlist[] = {
    [SEV_RET_SUCCESS]                = "",
    [SEV_RET_INVALID_PLATFORM_STATE] = "Platform state is invalid",
    [SEV_RET_INVALID_GUEST_STATE]    = "Guest state is invalid",
    [SEV_RET_INAVLID_CONFIG]         = "Platform configuration is invalid",
    [SEV_RET_INVALID_LEN]            = "Buffer too small",
    [SEV_RET_ALREADY_OWNED]          = "Platform is already owned",
    [SEV_RET_INVALID_CERTIFICATE]    = "Certificate is invalid",
    [SEV_RET_POLICY_FAILURE]         = "Policy is not allowed",
    [SEV_RET_INACTIVE]               = "Guest is not active",
    [SEV_RET_INVALID_ADDRESS]        = "Invalid address",
    [SEV_RET_BAD_SIGNATURE]          = "Bad signature",
    [SEV_RET_BAD_MEASUREMENT]        = "Bad measurement",
    [SEV_RET_ASID_OWNED]             = "ASID is already owned",
    [SEV_RET_INVALID_ASID]           = "Invalid ASID",
    [SEV_RET_WBINVD_REQUIRED]        = "WBINVD is required",
    [SEV_RET_DFFLUSH_REQUIRED]       = "DF_FLUSH is required",
    [SEV_RET_INVALID_GUEST]          = "Guest handle is invalid",
    [SEV_RET_INVALID_COMMAND]        = "Invalid command",
    [SEV_RET_ACTIVE]                 = "Guest is active",
    [SEV_RET_HWSEV_RET_PLATFORM]     = "Hardware error",
    [SEV_RET_HWSEV_RET_UNSAFE]       = "Hardware unsafe",
    [SEV_RET_UNSUPPORTED]            = "Feature not supported",
    [SEV_RET_INVALID_PARAM]          = "Invalid parameter",
    [SEV_RET_RESOURCE_LIMIT]         = "Required firmware resource depleted",
    [SEV_RET_SECURE_DATA_INVALID]    = "Part-specific integrity check failure",
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
sev_check_state(const SevCommonState *sev_common, SevState state)
{
    assert(sev_common);
    return sev_common->state == state ? true : false;
}

static void
sev_set_guest_state(SevCommonState *sev_common, SevState new_state)
{
    assert(new_state < SEV_STATE__MAX);
    assert(sev_common);

    trace_kvm_sev_change_state(SevState_str(sev_common->state),
                               SevState_str(new_state));
    sev_common->state = new_state;
}

static void
sev_ram_block_added(RAMBlockNotifier *n, void *host, size_t size,
                    size_t max_size)
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
    range.size = max_size;

    trace_kvm_memcrypt_register_region(host, max_size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_REG_REGION, &range);
    if (r) {
        error_report("%s: failed to register region (%p+%#zx) error '%s'",
                     __func__, host, max_size, strerror(errno));
        exit(1);
    }
}

static void
sev_ram_block_removed(RAMBlockNotifier *n, void *host, size_t size,
                      size_t max_size)
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
    range.size = max_size;

    trace_kvm_memcrypt_unregister_region(host, max_size);
    r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_UNREG_REGION, &range);
    if (r) {
        error_report("%s: failed to unregister region (%p+%#zx)",
                     __func__, host, max_size);
    }
}

static struct RAMBlockNotifier sev_ram_notifier = {
    .ram_block_added = sev_ram_block_added,
    .ram_block_removed = sev_ram_block_removed,
};

static char *
sev_common_get_sev_device(Object *obj, Error **errp)
{
    return g_strdup(SEV_COMMON(obj)->sev_device);
}

static void
sev_common_set_sev_device(Object *obj, const char *value, Error **errp)
{
    SEV_COMMON(obj)->sev_device = g_strdup(value);
}

static void
sev_common_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "sev-device",
                                  sev_common_get_sev_device,
                                  sev_common_set_sev_device);
    object_class_property_set_description(oc, "sev-device",
            "SEV device to use");
}

static void
sev_common_instance_init(Object *obj)
{
    SevCommonState *sev_common = SEV_COMMON(obj);

    sev_common->sev_device = g_strdup(DEFAULT_SEV_DEVICE);

    object_property_add_uint32_ptr(obj, "cbitpos", &sev_common->cbitpos,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "reduced-phys-bits",
                                   &sev_common->reduced_phys_bits,
                                   OBJ_PROP_FLAG_READWRITE);
}

/* sev guest info common to sev/sev-es/sev-snp */
static const TypeInfo sev_common_info = {
    .parent = TYPE_CONFIDENTIAL_GUEST_SUPPORT,
    .name = TYPE_SEV_COMMON,
    .instance_size = sizeof(SevCommonState),
    .class_init = sev_common_class_init,
    .instance_init = sev_common_instance_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static char *
sev_guest_get_dh_cert_file(Object *obj, Error **errp)
{
    return g_strdup(SEV_GUEST(obj)->dh_cert_file);
}

static void
sev_guest_set_dh_cert_file(Object *obj, const char *value, Error **errp)
{
    SEV_GUEST(obj)->dh_cert_file = g_strdup(value);
}

static char *
sev_guest_get_session_file(Object *obj, Error **errp)
{
    SevGuestState *sev_guest = SEV_GUEST(obj);

    return sev_guest->session_file ? g_strdup(sev_guest->session_file) : NULL;
}

static void
sev_guest_set_session_file(Object *obj, const char *value, Error **errp)
{
    SEV_GUEST(obj)->session_file = g_strdup(value);
}

static void
sev_guest_class_init(ObjectClass *oc, void *data)
{
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
    SevGuestState *sev_guest = SEV_GUEST(obj);

    sev_guest->policy = DEFAULT_GUEST_POLICY;
    object_property_add_uint32_ptr(obj, "handle", &sev_guest->handle,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "policy", &sev_guest->policy,
                                   OBJ_PROP_FLAG_READWRITE);
}

/* guest info specific sev/sev-es */
static const TypeInfo sev_guest_info = {
    .parent = TYPE_SEV_COMMON,
    .name = TYPE_SEV_GUEST,
    .instance_size = sizeof(SevGuestState),
    .instance_init = sev_guest_instance_init,
    .class_init = sev_guest_class_init,
};

static void
sev_snp_guest_get_init_flags(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    visit_type_uint64(v, name,
                      (uint64_t *)&SEV_SNP_GUEST(obj)->kvm_init_conf.flags,
                      errp);
}

static void
sev_snp_guest_set_init_flags(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    visit_type_uint64(v, name,
                      (uint64_t *)&SEV_SNP_GUEST(obj)->kvm_init_conf.flags,
                      errp);
}

static void
sev_snp_guest_get_policy(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    visit_type_uint64(v, name,
                      (uint64_t *)&SEV_SNP_GUEST(obj)->kvm_start_conf.policy,
                      errp);
}

static void
sev_snp_guest_set_policy(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    visit_type_uint64(v, name,
                      (uint64_t *)&SEV_SNP_GUEST(obj)->kvm_start_conf.policy,
                      errp);
}

static char *
sev_snp_guest_get_guest_visible_workarounds(Object *obj, Error **errp)
{
    return g_strdup(SEV_SNP_GUEST(obj)->guest_visible_workarounds);
}

static void
sev_snp_guest_set_guest_visible_workarounds(Object *obj, const char *value,
                                            Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_start *start = &sev_snp_guest->kvm_start_conf;
    g_autofree guchar *blob;
    gsize len;

    if (sev_snp_guest->guest_visible_workarounds) {
        g_free(sev_snp_guest->guest_visible_workarounds);
    }

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->guest_visible_workarounds = g_strdup(value);

    blob = g_base64_decode(sev_snp_guest->guest_visible_workarounds, &len);
    if (len > sizeof(start->gosvw)) {
        error_setg(errp, "parameter length of %lu exceeds max of %lu",
                   len, sizeof(start->gosvw));
        return;
    }

    memcpy(start->gosvw, blob, len);
}

static char *
sev_snp_guest_get_id_block(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return g_strdup(sev_snp_guest->id_block);
}

static void
sev_snp_guest_set_id_block(Object *obj, const char *value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp_guest->kvm_finish_conf;
    gsize len;

    if (sev_snp_guest->id_block) {
        g_free(sev_snp_guest->id_block);
        g_free((guchar *)finish->id_block_uaddr);
    }

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->id_block = g_strdup(value);

    finish->id_block_uaddr = (uint64_t)g_base64_decode(sev_snp_guest->id_block, &len);
    if (len > KVM_SEV_SNP_ID_BLOCK_SIZE) {
        error_setg(errp, "parameter length of %lu exceeds max of %u",
                   len, KVM_SEV_SNP_ID_BLOCK_SIZE);
        return;
    }
    finish->id_block_en = 1;
}

static char *
sev_snp_guest_get_id_auth(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return g_strdup(sev_snp_guest->id_auth);
}

static void
sev_snp_guest_set_id_auth(Object *obj, const char *value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp_guest->kvm_finish_conf;
    gsize len;

    if (sev_snp_guest->id_auth) {
        g_free(sev_snp_guest->id_auth);
        g_free((guchar *)finish->id_auth_uaddr);
    }

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->id_auth = g_strdup(value);

    finish->id_auth_uaddr = (uint64_t)g_base64_decode(sev_snp_guest->id_auth, &len);
    if (len > KVM_SEV_SNP_ID_AUTH_SIZE) {
        error_setg(errp, "parameter length of %lu exceeds max of %u",
                   len, KVM_SEV_SNP_ID_AUTH_SIZE);
        return;
    }
}

static bool
sev_snp_guest_get_auth_key_en(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return !!sev_snp_guest->kvm_finish_conf.auth_key_en;
}

static void
sev_snp_guest_set_auth_key_en(Object *obj, bool value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    sev_snp_guest->kvm_finish_conf.auth_key_en = value;
}

static char *
sev_snp_guest_get_host_data(Object *obj, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    return g_strdup(sev_snp_guest->host_data);
}

static void
sev_snp_guest_set_host_data(Object *obj, const char *value, Error **errp)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);
    struct kvm_sev_snp_launch_finish *finish = &sev_snp_guest->kvm_finish_conf;
    g_autofree guchar *blob;
    gsize len;

    if (sev_snp_guest->host_data) {
        g_free(sev_snp_guest->host_data);
    }

    /* store the base64 str so we don't need to re-encode in getter */
    sev_snp_guest->host_data = g_strdup(value);

    blob = g_base64_decode(sev_snp_guest->host_data, &len);
    if (len > sizeof(finish->host_data)) {
        error_setg(errp, "parameter length of %lu exceeds max of %lu",
                   len, sizeof(finish->host_data));
        return;
    }

    memcpy(finish->host_data, blob, len);
}

static void
sev_snp_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add(oc, "init-flags", "uint64",
                              sev_snp_guest_get_init_flags,
                              sev_snp_guest_set_init_flags, NULL, NULL);
    object_class_property_set_description(oc, "init-flags",
        "guest initialization flags");
    object_class_property_add(oc, "policy", "uint64",
                              sev_snp_guest_get_policy,
                              sev_snp_guest_set_policy, NULL, NULL);
    object_class_property_add_str(oc, "guest-visible-workarounds",
                                  sev_snp_guest_get_guest_visible_workarounds,
                                  sev_snp_guest_set_guest_visible_workarounds);
    object_class_property_add_str(oc, "id-block",
                                  sev_snp_guest_get_id_block,
                                  sev_snp_guest_set_id_block);
    object_class_property_add_str(oc, "id-auth",
                                  sev_snp_guest_get_id_auth,
                                  sev_snp_guest_set_id_auth);
    object_class_property_add_bool(oc, "auth-key-enabled",
                                   sev_snp_guest_get_auth_key_en,
                                   sev_snp_guest_set_auth_key_en);
    object_class_property_add_str(oc, "host-data",
                                  sev_snp_guest_get_host_data,
                                  sev_snp_guest_set_host_data);
}

static void
sev_snp_guest_instance_init(Object *obj)
{
    SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(obj);

    /* default init/start/finish params for kvm */
    sev_snp_guest->kvm_start_conf.policy = DEFAULT_SEV_SNP_POLICY;
}

/* guest info specific to sev-snp */
static const TypeInfo sev_snp_guest_info = {
    .parent = TYPE_SEV_COMMON,
    .name = TYPE_SEV_SNP_GUEST,
    .instance_size = sizeof(SevSnpGuestState),
    .class_init = sev_snp_guest_class_init,
    .instance_init = sev_snp_guest_instance_init,
};

bool
sev_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return !!object_dynamic_cast(OBJECT(cgs), TYPE_SEV_COMMON);
}

bool
sev_snp_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return !!object_dynamic_cast(OBJECT(cgs), TYPE_SEV_SNP_GUEST);
}

bool
sev_es_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return sev_snp_enabled() ||
            (sev_enabled() && SEV_GUEST(cgs)->policy & SEV_POLICY_ES);
}

uint64_t
sev_get_me_mask(void)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    return sev_common ? sev_common->me_mask : ~0;
}

uint32_t
sev_get_cbit_position(void)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    return sev_common ? sev_common->cbitpos : 0;
}

uint32_t
sev_get_reduced_phys_bits(void)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    return sev_common ? sev_common->reduced_phys_bits : 0;
}

SevInfo *
sev_get_info(void)
{
    SevInfo *info;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    SevGuestState *sev_guest =
        (SevGuestState *)object_dynamic_cast(OBJECT(sev_common),
                                             TYPE_SEV_GUEST);

    info = g_new0(SevInfo, 1);
    info->enabled = sev_enabled();

    if (info->enabled) {
        if (sev_guest) {
            info->handle = sev_guest->handle;
        }
        info->api_major = sev_common->api_major;
        info->api_minor = sev_common->api_minor;
        info->build_id = sev_common->build_id;
        info->state = sev_common->state;
        /* we only report the lower 32-bits of policy for SNP, ok for now... */
        info->policy =
            (uint32_t)object_property_get_uint(OBJECT(sev_common),
                                               "policy", NULL);
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
    SevCommonState *sev_common;
    char *sev_device;

    if (!kvm_enabled()) {
        error_setg(errp, "KVM not enabled");
        return NULL;
    }
    if (kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, NULL) < 0) {
        error_setg(errp, "SEV is not enabled in KVM");
        return NULL;
    }

    sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    if (!sev_common) {
        error_setg(errp, "SEV is not configured");
    }

    sev_device = object_property_get_str(OBJECT(sev_common), "sev-device",
                                         &error_abort);
    fd = open(sev_device, O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to open %s",
                         DEFAULT_SEV_DEVICE);
        g_free(sev_device);
        return NULL;
    }
    g_free(sev_device);

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

SevAttestationReport *
sev_get_attestation_report(const char *mnonce, Error **errp)
{
    struct kvm_sev_attestation_report input = {};
    SevAttestationReport *report = NULL;
    SevCommonState *sev_common;
    guchar *data;
    guchar *buf;
    gsize len;
    int err = 0, ret;

    if (!sev_enabled()) {
        error_setg(errp, "SEV is not enabled");
        return NULL;
    }

    /* lets decode the mnonce string */
    buf = g_base64_decode(mnonce, &len);
    if (!buf) {
        error_setg(errp, "SEV: failed to decode mnonce input");
        return NULL;
    }

    /* verify the input mnonce length */
    if (len != sizeof(input.mnonce)) {
        error_setg(errp, "SEV: mnonce must be %zu bytes (got %" G_GSIZE_FORMAT ")",
                sizeof(input.mnonce), len);
        g_free(buf);
        return NULL;
    }

    sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    /* Query the report length */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_GET_ATTESTATION_REPORT,
            &input, &err);
    if (ret < 0) {
        if (err != SEV_RET_INVALID_LEN) {
            error_setg(errp, "failed to query the attestation report length "
                    "ret=%d fw_err=%d (%s)", ret, err, fw_error_to_str(err));
            g_free(buf);
            return NULL;
        }
    }

    data = g_malloc(input.len);
    input.uaddr = (unsigned long)data;
    memcpy(input.mnonce, buf, sizeof(input.mnonce));

    /* Query the report */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_GET_ATTESTATION_REPORT,
            &input, &err);
    if (ret) {
        error_setg_errno(errp, errno, "Failed to get attestation report"
                " ret=%d fw_err=%d (%s)", ret, err, fw_error_to_str(err));
        goto e_free_data;
    }

    report = g_new0(SevAttestationReport, 1);
    report->data = g_base64_encode(data, input.len);

    trace_kvm_sev_attestation_report(mnonce, report->data);

e_free_data:
    g_free(data);
    g_free(buf);
    return report;
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
sev_snp_launch_start(SevSnpGuestState *sev_snp_guest)
{
    int fw_error, rc;
    SevCommonState *sev_common = SEV_COMMON(sev_snp_guest);
    struct kvm_sev_snp_launch_start *start = &sev_snp_guest->kvm_start_conf;

    trace_kvm_sev_snp_launch_start(start->policy);

    rc = sev_ioctl(sev_common->sev_fd, KVM_SEV_SNP_LAUNCH_START,
                   start, &fw_error);
    if (rc < 0) {
        error_report("%s: SNP_LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, rc, fw_error, fw_error_to_str(fw_error));
        return 1;
    }

    sev_set_guest_state(sev_common, SEV_STATE_LAUNCH_UPDATE);

    return 0;
}

static int
sev_launch_start(SevGuestState *sev_guest)
{
    gsize sz;
    int ret = 1;
    int fw_error, rc;
    struct kvm_sev_launch_start *start;
    guchar *session = NULL, *dh_cert = NULL;
    SevCommonState *sev_common = SEV_COMMON(sev_guest);

    start = g_new0(struct kvm_sev_launch_start, 1);

    start->handle = sev_guest->handle;
    start->policy = sev_guest->policy;
    if (sev_guest->session_file) {
        if (sev_read_file_base64(sev_guest->session_file, &session, &sz) < 0) {
            goto out;
        }
        start->session_uaddr = (unsigned long)session;
        start->session_len = sz;
    }

    if (sev_guest->dh_cert_file) {
        if (sev_read_file_base64(sev_guest->dh_cert_file, &dh_cert, &sz) < 0) {
            goto out;
        }
        start->dh_uaddr = (unsigned long)dh_cert;
        start->dh_len = sz;
    }

    trace_kvm_sev_launch_start(start->policy, session, dh_cert);
    rc = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_START, start, &fw_error);
    if (rc < 0) {
        error_report("%s: LAUNCH_START ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto out;
    }

    sev_set_guest_state(sev_common, SEV_STATE_LAUNCH_UPDATE);
    sev_guest->handle = start->handle;
    ret = 0;

out:
    g_free(start);
    g_free(session);
    g_free(dh_cert);
    return ret;
}

static int
sev_snp_launch_update(SevSnpGuestState *sev_snp_guest, hwaddr gpa, uint8_t *addr,
                      uint64_t len, int type)
{
    int ret, fw_error;
    struct kvm_sev_snp_launch_update update = {0};

    if (!addr || !len) {
        error_report("%s: SNP_LAUNCH_UPDATE called with invalid address / length: %lx / %lx",
                __func__, gpa, len);
        return 1;
    }

    update.uaddr = (__u64)(unsigned long)addr;
    update.start_gfn = gpa >> TARGET_PAGE_BITS;
    update.len = len;
    update.page_type = type;
    trace_kvm_sev_snp_launch_update(addr, len, type);
    ret = sev_ioctl(SEV_COMMON(sev_snp_guest)->sev_fd,
                    KVM_SEV_SNP_LAUNCH_UPDATE,
                    &update, &fw_error);
    if (ret) {
        error_report("%s: SNP_LAUNCH_UPDATE ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static int
sev_launch_update_data(SevGuestState *sev_guest, uint8_t *addr, uint64_t len)
{
    int ret, fw_error;
    struct kvm_sev_launch_update_data update;

    if (!addr || !len) {
        return 1;
    }

    update.uaddr = (__u64)(unsigned long)addr;
    update.len = len;
    trace_kvm_sev_launch_update_data(addr, len);
    ret = sev_ioctl(SEV_COMMON(sev_guest)->sev_fd, KVM_SEV_LAUNCH_UPDATE_DATA,
                    &update, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static int
sev_launch_update_vmsa(SevGuestState *sev_guest)
{
    int ret, fw_error;

    ret = sev_ioctl(SEV_COMMON(sev_guest)->sev_fd, KVM_SEV_LAUNCH_UPDATE_VMSA,
                    NULL, &fw_error);
    if (ret) {
        error_report("%s: LAUNCH_UPDATE_VMSA ret=%d fw_error=%d '%s'",
                __func__, ret, fw_error, fw_error_to_str(fw_error));
    }

    return ret;
}

static void
sev_launch_get_measure(Notifier *notifier, void *unused)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);
    SevGuestState *sev_guest = SEV_GUEST(sev_common);
    int ret, error;
    guchar *data;
    struct kvm_sev_launch_measure *measurement;

    if (!sev_check_state(sev_common, SEV_STATE_LAUNCH_UPDATE)) {
        return;
    }

    if (sev_es_enabled()) {
        /* measure all the VM save areas before getting launch_measure */
        ret = sev_launch_update_vmsa(sev_guest);
        if (ret) {
            exit(1);
        }
    }

    measurement = g_new0(struct kvm_sev_launch_measure, 1);

    /* query the measurement blob length */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    measurement, &error);
    if (!measurement->len) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        goto free_measurement;
    }

    data = g_new0(guchar, measurement->len);
    measurement->uaddr = (unsigned long)data;

    /* get the measurement blob */
    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_MEASURE,
                    measurement, &error);
    if (ret) {
        error_report("%s: LAUNCH_MEASURE ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(errno));
        goto free_data;
    }

    sev_set_guest_state(sev_common, SEV_STATE_LAUNCH_SECRET);

    /* encode the measurement value and emit the event */
    sev_guest->measurement = g_base64_encode(data, measurement->len);
    trace_kvm_sev_launch_measurement(sev_guest->measurement);

free_data:
    g_free(data);
free_measurement:
    g_free(measurement);
}

char *
sev_get_launch_measurement(void)
{
    SevGuestState *sev_guest = SEV_GUEST(MACHINE(qdev_get_machine())->cgs);

    if (sev_guest &&
        SEV_COMMON(sev_guest)->state >= SEV_STATE_LAUNCH_SECRET) {
        return g_strdup(sev_guest->measurement);
    }

    return NULL;
}

static Notifier sev_machine_done_notify = {
    .notify = sev_launch_get_measure,
};

static int
sev_snp_launch_update_gpa(uint32_t hwaddr, uint32_t size, uint8_t type)
{
    void *hva;
    MemoryRegion *mr = NULL;
    SevSnpGuestState *sev_snp_guest =
        SEV_SNP_GUEST(MACHINE(qdev_get_machine())->cgs);

    hva = gpa2hva(&mr, hwaddr, size, NULL);
    if (!hva) {
        error_report("SEV-SNP failed to get HVA for GPA 0x%x", hwaddr);
        return 1;
    }

    return sev_snp_launch_update(sev_snp_guest, hwaddr, hva, size, type);
}

static bool
detect_first_overlap(uint64_t start, uint64_t end, Range *range_list,
                     size_t range_count, Range *overlap_range)
{
    int i;
    bool overlap = false;
    Range new;

    assert(overlap_range);
    range_make_empty(overlap_range);
    range_init_nofail(&new, start, end - start + 1);

    for (i = 0; i < range_count; i++) {
        if (range_overlaps_range(&new, &range_list[i]) &&
            (range_is_empty(overlap_range) ||
             range_lob(&range_list[i]) < range_lob(overlap_range))) {
            *overlap_range = range_list[i];
            overlap = true;
        }
    }

    return overlap;
}

static void snp_ovmf_boot_block_setup(void)
{
    SevSnpBootInfoBlock *info;
    uint32_t start, end, sz;
    int ret;
    Range validated_ranges[2];

    /*
     * Extract the SNP boot block for the SEV-SNP guests by locating the
     * SNP_BOOT GUID. The boot block contains the information such as location
     * of secrets and CPUID page, additionaly it may contain the range of
     * memory that need to be pre-validated for the boot.
     */
    if (!pc_system_ovmf_table_find(SEV_SNP_BOOT_BLOCK_GUID,
        (uint8_t **)&info, NULL)) {
        error_report("SEV-SNP: failed to find the SNP boot block");
        exit(1);
    }

    trace_kvm_sev_snp_ovmf_boot_block_info(info->secrets_addr,
                                           info->secrets_len, info->cpuid_addr,
                                           info->cpuid_len,
                                           info->pre_validated_start,
                                           info->pre_validated_end);

    /* Populate the secrets page */
    ret = sev_snp_launch_update_gpa(info->secrets_addr, info->secrets_len,
                                    KVM_SEV_SNP_PAGE_TYPE_SECRETS);
    if (ret) {
        error_report("SEV-SNP: failed to insert secret page GPA 0x%x",
                     info->secrets_addr);
        exit(1);
    }

    /* Populate the cpuid page */
    ret = sev_snp_launch_update_gpa(info->cpuid_addr, info->cpuid_len,
                                    KVM_SEV_SNP_PAGE_TYPE_CPUID);
    if (ret) {
        error_report("SEV-SNP: failed to insert cpuid page GPA 0x%x",
                     info->cpuid_addr);
        exit(1);
    }

    /*
     * Pre-validate the range using the LAUNCH_UPDATE_DATA, if the
     * pre-validation range contains the CPUID and Secret page GPA then skip
     * it. This is because SEV-SNP firmware pre-validates those pages as part
     * of adding secrets and cpuid LAUNCH_UPDATE type.
     */
    range_init_nofail(&validated_ranges[0], info->secrets_addr, info->secrets_len);
    range_init_nofail(&validated_ranges[1], info->cpuid_addr, info->cpuid_len);
    start = info->pre_validated_start;
    end = info->pre_validated_end;

    while (start < end) {
        Range overlap_range;

        /* Check if the requested range overlaps with Secrets and CPUID page */
        if (detect_first_overlap(start, end, validated_ranges, 2,
                                 &overlap_range)) {
            if (start < range_lob(&overlap_range)) {
                sz = range_lob(&overlap_range) - start;
                if (sev_snp_launch_update_gpa(start, sz,
                    KVM_SEV_SNP_PAGE_TYPE_UNMEASURED)) {
                    error_report("SEV-SNP: failed to validate gpa 0x%x sz %d",
                                 start, sz);
                    exit(1);
                }
            }

            start = range_upb(&overlap_range) + 1;
            continue;
        }

        /* Validate the remaining range */
        if (sev_snp_launch_update_gpa(start, end - start,
            KVM_SEV_SNP_PAGE_TYPE_UNMEASURED)) {
            error_report("SEV-SNP: failed to validate gpa 0x%x sz %d",
                         start, end - start);
            exit(1);
        }

        start = end;
    }
}

static void
sev_snp_launch_finish(SevSnpGuestState *sev_snp)
{
    int ret, error;
    Error *local_err = NULL;
    struct kvm_sev_snp_launch_finish *finish = &sev_snp->kvm_finish_conf;

    trace_kvm_sev_snp_launch_finish();
    ret = sev_ioctl(SEV_COMMON(sev_snp)->sev_fd, KVM_SEV_SNP_LAUNCH_FINISH, finish, &error);
    if (ret) {
        error_report("%s: SNP_LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
        exit(1);
    }

    sev_set_guest_state(SEV_COMMON(sev_snp), SEV_STATE_RUNNING);

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
sev_launch_finish(SevGuestState *sev_guest)
{
    int ret, error;

    trace_kvm_sev_launch_finish();
    ret = sev_ioctl(SEV_COMMON(sev_guest)->sev_fd, KVM_SEV_LAUNCH_FINISH, 0,
                    &error);
    if (ret) {
        error_report("%s: LAUNCH_FINISH ret=%d fw_error=%d '%s'",
                     __func__, ret, error, fw_error_to_str(error));
        exit(1);
    }

    sev_set_guest_state(SEV_COMMON(sev_guest), SEV_STATE_RUNNING);

    /* add migration blocker */
    error_setg(&sev_mig_blocker,
               "SEV: Migration is not implemented");
    migrate_add_blocker(sev_mig_blocker, &error_fatal);
}

static void
sev_vm_state_change(void *opaque, bool running, RunState state)
{
    SevCommonState *sev_common = opaque;

    if (running) {
        if (!sev_check_state(sev_common, SEV_STATE_RUNNING)) {
            if (sev_snp_enabled()) {
                snp_ovmf_boot_block_setup();
                sev_snp_launch_finish(SEV_SNP_GUEST(sev_common));
            } else {
                sev_launch_finish(SEV_GUEST(sev_common));
            }
        }
    }
}

int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    SevCommonState *sev_common = SEV_COMMON(cgs);
    char *devname;
    int ret, fw_error, cmd;
    uint32_t ebx;
    uint32_t host_cbitpos;
    struct sev_user_data_status status = {};
    void *init_args = NULL;

    if (!sev_common) {
        return 0;
    }

    ret = ram_block_discard_disable(true);
    if (ret) {
        error_report("%s: cannot disable RAM discard", __func__);
        return -1;
    }

    sev_common->state = SEV_STATE_UNINIT;

    host_cpuid(0x8000001F, 0, NULL, &ebx, NULL, NULL);
    host_cbitpos = ebx & 0x3f;

    if (host_cbitpos != sev_common->cbitpos) {
        error_setg(errp, "%s: cbitpos check failed, host '%d' requested '%d'",
                   __func__, host_cbitpos, sev_common->cbitpos);
        goto err;
    }

    if (sev_common->reduced_phys_bits < 1) {
        error_setg(errp, "%s: reduced_phys_bits check failed, it should be >=1,"
                   " requested '%d'", __func__, sev_common->reduced_phys_bits);
        goto err;
    }

    sev_common->me_mask = ~(1UL << sev_common->cbitpos);

    devname = object_property_get_str(OBJECT(sev_common), "sev-device", NULL);
    sev_common->sev_fd = open(devname, O_RDWR);
    if (sev_common->sev_fd < 0) {
        error_setg(errp, "%s: Failed to open %s '%s'", __func__,
                   devname, strerror(errno));
        g_free(devname);
        goto err;
    }
    g_free(devname);

    ret = sev_platform_ioctl(sev_common->sev_fd, SEV_PLATFORM_STATUS, &status,
                             &fw_error);
    if (ret) {
        error_setg(errp, "%s: failed to get platform status ret=%d "
                   "fw_error='%d: %s'", __func__, ret, fw_error,
                   fw_error_to_str(fw_error));
        goto err;
    }
    sev_common->build_id = status.build;
    sev_common->api_major = status.api_major;
    sev_common->api_minor = status.api_minor;

    if (sev_snp_enabled()) {
        SevSnpGuestState *sev_snp_guest = SEV_SNP_GUEST(sev_common);
        if (!kvm_kernel_irqchip_allowed()) {
            error_report("%s: SEV-SNP guests require in-kernel irqchip support",
                         __func__);
            goto err;
        }

        cmd = KVM_SEV_SNP_INIT;
        init_args = (void *)&sev_snp_guest->kvm_init_conf;

    } else if (sev_es_enabled()) {
        if (!kvm_kernel_irqchip_allowed()) {
            error_report("%s: SEV-ES guests require in-kernel irqchip support",
                         __func__);
            goto err;
        }

        if (!(status.flags & SEV_STATUS_FLAGS_CONFIG_ES)) {
            error_report("%s: guest policy requires SEV-ES, but "
                         "host SEV-ES support unavailable",
                         __func__);
            goto err;
        }
        cmd = KVM_SEV_ES_INIT;
    } else {
        cmd = KVM_SEV_INIT;
    }

    trace_kvm_sev_init();
    ret = sev_ioctl(sev_common->sev_fd, cmd, init_args, &fw_error);
    if (ret) {
        error_setg(errp, "%s: failed to initialize ret=%d fw_error=%d '%s'",
                   __func__, ret, fw_error, fw_error_to_str(fw_error));
        goto err;
    }

    if (sev_snp_enabled()) {
        ret = sev_snp_launch_start(SEV_SNP_GUEST(sev_common));
    } else {
        ret = sev_launch_start(SEV_GUEST(sev_common));
    }

    if (ret) {
        error_setg(errp, "%s: failed to create encryption context", __func__);
        goto err;
    }

    ram_block_notifier_add(&sev_ram_notifier);

    /*
     * The machine done notify event is used by the SEV guest to get the
     * measurement of the encrypted images. When SEV-SNP is enabled then
     * measurement is part of the attestation report and the measurement
     * command does not exist. So skip registering the notifier.
     */
    if (!sev_snp_enabled()) {
        qemu_add_machine_init_done_notifier(&sev_machine_done_notify);
    }

    qemu_add_vm_change_state_handler(sev_vm_state_change, sev_common);

    cgs->ready = true;

    return 0;
err:
    ram_block_discard_disable(false);
    return -1;
}

int
sev_encrypt_flash(hwaddr gpa, uint8_t *ptr, uint64_t len, Error **errp)
{
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    if (!sev_common) {
        return 0;
    }

    /* if SEV is in update state then encrypt the data else do nothing */
    if (sev_check_state(sev_common, SEV_STATE_LAUNCH_UPDATE)) {
        int ret;

        if (sev_snp_enabled()) {
            ret = sev_snp_launch_update(SEV_SNP_GUEST(sev_common), gpa, ptr,
                                        len, KVM_SEV_SNP_PAGE_TYPE_NORMAL);
        } else {
            ret = sev_launch_update_data(SEV_GUEST(sev_common), ptr, len);
        }
        if (ret < 0) {
            error_setg(errp, "failed to encrypt pflash rom");
            return ret;
        }
    }

    return 0;
}

int sev_inject_launch_secret(const char *packet_hdr, const char *secret,
                             uint64_t gpa, Error **errp)
{
    struct kvm_sev_launch_secret input;
    g_autofree guchar *data = NULL, *hdr = NULL;
    int error, ret = 1;
    void *hva;
    gsize hdr_sz = 0, data_sz = 0;
    MemoryRegion *mr = NULL;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    if (!sev_common) {
        error_setg(errp, "SEV: SEV not enabled.");
        return 1;
    }

    /* secret can be injected only in this state */
    if (!sev_check_state(sev_common, SEV_STATE_LAUNCH_SECRET)) {
        error_setg(errp, "SEV: Not in correct state. (LSECRET) %x",
                   sev_common->state);
        return 1;
    }

    hdr = g_base64_decode(packet_hdr, &hdr_sz);
    if (!hdr || !hdr_sz) {
        error_setg(errp, "SEV: Failed to decode sequence header");
        return 1;
    }

    data = g_base64_decode(secret, &data_sz);
    if (!data || !data_sz) {
        error_setg(errp, "SEV: Failed to decode data");
        return 1;
    }

    hva = gpa2hva(&mr, gpa, data_sz, errp);
    if (!hva) {
        error_prepend(errp, "SEV: Failed to calculate guest address: ");
        return 1;
    }

    input.hdr_uaddr = (uint64_t)(unsigned long)hdr;
    input.hdr_len = hdr_sz;

    input.trans_uaddr = (uint64_t)(unsigned long)data;
    input.trans_len = data_sz;

    input.guest_uaddr = (uint64_t)(unsigned long)hva;
    input.guest_len = data_sz;

    trace_kvm_sev_launch_secret(gpa, input.guest_uaddr,
                                input.trans_uaddr, input.trans_len);

    ret = sev_ioctl(sev_common->sev_fd, KVM_SEV_LAUNCH_SECRET,
                    &input, &error);
    if (ret) {
        error_setg(errp, "SEV: failed to inject secret ret=%d fw_error=%d '%s'",
                     ret, error, fw_error_to_str(error));
        return ret;
    }

    return 0;
}

static int
sev_es_parse_reset_block(SevInfoBlock *info, uint32_t *addr)
{
    if (!info->reset_addr) {
        error_report("SEV-ES reset address is zero");
        return 1;
    }

    *addr = info->reset_addr;

    return 0;
}

static int
sev_es_find_reset_vector(void *flash_ptr, uint64_t flash_size,
                         uint32_t *addr)
{
    QemuUUID info_guid, *guid;
    SevInfoBlock *info;
    uint8_t *data;
    uint16_t *len;

    /*
     * Initialize the address to zero. An address of zero with a successful
     * return code indicates that SEV-ES is not active.
     */
    *addr = 0;

    /*
     * Extract the AP reset vector for SEV-ES guests by locating the SEV GUID.
     * The SEV GUID is located on its own (original implementation) or within
     * the Firmware GUID Table (new implementation), either of which are
     * located 32 bytes from the end of the flash.
     *
     * Check the Firmware GUID Table first.
     */
    if (pc_system_ovmf_table_find(SEV_INFO_BLOCK_GUID, &data, NULL)) {
        return sev_es_parse_reset_block((SevInfoBlock *)data, addr);
    }

    /*
     * SEV info block not found in the Firmware GUID Table (or there isn't
     * a Firmware GUID Table), fall back to the original implementation.
     */
    data = flash_ptr + flash_size - 0x20;

    qemu_uuid_parse(SEV_INFO_BLOCK_GUID, &info_guid);
    info_guid = qemu_uuid_bswap(info_guid); /* GUIDs are LE */

    guid = (QemuUUID *)(data - sizeof(info_guid));
    if (!qemu_uuid_is_equal(guid, &info_guid)) {
        error_report("SEV information block/Firmware GUID Table block not found in pflash rom");
        return 1;
    }

    len = (uint16_t *)((uint8_t *)guid - sizeof(*len));
    info = (SevInfoBlock *)(data - le16_to_cpu(*len));

    return sev_es_parse_reset_block(info, addr);
}

void sev_es_set_reset_vector(CPUState *cpu)
{
    X86CPU *x86;
    CPUX86State *env;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    /* Only update if we have valid reset information */
    if (!sev_common || !sev_common->reset_data_valid) {
        return;
    }

    /* Do not update the BSP reset state */
    if (cpu->cpu_index == 0) {
        return;
    }

    x86 = X86_CPU(cpu);
    env = &x86->env;

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, sev_common->reset_cs, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);

    env->eip = sev_common->reset_ip;
}

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size)
{
    CPUState *cpu;
    uint32_t addr;
    int ret;
    SevCommonState *sev_common = SEV_COMMON(MACHINE(qdev_get_machine())->cgs);

    if (!sev_es_enabled()) {
        return 0;
    }

    addr = 0;
    ret = sev_es_find_reset_vector(flash_ptr, flash_size,
                                   &addr);
    if (ret) {
        return ret;
    }

    if (addr) {
        sev_common->reset_cs = addr & 0xffff0000;
        sev_common->reset_ip = addr & 0x0000ffff;
        sev_common->reset_data_valid = true;

        CPU_FOREACH(cpu) {
            sev_es_set_reset_vector(cpu);
        }
    }

    return 0;
}

static void
sev_register_types(void)
{
    type_register_static(&sev_common_info);
    type_register_static(&sev_guest_info);
    type_register_static(&sev_snp_guest_info);
}

type_init(sev_register_types);
