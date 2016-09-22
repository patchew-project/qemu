/*
 * QEMU SEV support
 *
 * Copyright Advanced Micro Devices 2016
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
#include "trace.h"

#define DEBUG_SEV
#ifdef DEBUG_SEV
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static MemoryRegionRAMReadWriteOps sev_ops;
static bool sev_allowed;

static void
DPRINTF_U8_PTR(const char *msg, uint8_t *ptr, int count)
{
    int i;

    DPRINTF("%s = ", msg);
    for (i = 0; i < count; i++) {
        DPRINTF("%02hhx", ptr[i]);
    }
    DPRINTF("\n");
}

static void
str_to_uint8_ptr(const char *str, uint8_t *ptr, int count)
{
    int i = 0;

    while (*str && i != count) {
        sscanf(str, "%2hhx", &ptr[i]);
        str += 2;
        i++;
    }
}

static char *
uint8_ptr_to_str(uint8_t *ptr, int count)
{
    char *str = g_malloc0(count);

    return memcpy(str, ptr, count);
}

static Object *
get_object_by_id(const char *id)
{
    Object *obj;

    obj = object_resolve_path_component(
        object_get_objects_root(), id);
    if (!obj) {
        return NULL;
    }

    return obj;

}

static bool
qsev_policy_get_debug(Object *obj, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    return policy->debug;
}

static void
qsev_policy_set_debug(Object *obj, bool value, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    policy->debug = value;
}

static bool
qsev_policy_get_ks(Object *obj, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    return policy->ks;
}

static void
qsev_policy_set_ks(Object *obj, bool value, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    policy->ks = value;
}

static bool
qsev_policy_get_nosend(Object *obj, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    return policy->nosend;
}

static void
qsev_policy_set_nosend(Object *obj, bool value, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    policy->nosend = value;
}

static bool
qsev_policy_get_domain(Object *obj, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    return policy->domain;
}

static void
qsev_policy_set_domain(Object *obj, bool value, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    policy->domain = value;
}

static bool
qsev_policy_get_sev(Object *obj, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    return policy->sev;
}

static void
qsev_policy_set_sev(Object *obj, bool value, Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);

    policy->sev = value;
}

static void
qsev_policy_get_fw_major(Object *obj, Visitor *v,
                         const char *name, void *opaque,
                         Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);
    uint8_t value = policy->fw_major;

    visit_type_uint8(v, name, &value, errp);
}

static void
qsev_policy_set_fw_major(Object *obj, Visitor *v,
                         const char *name, void *opaque,
                         Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);
    Error *error = NULL;
    uint8_t value;

    visit_type_uint8(v, name, &value, &error);
    if (error) {
        return;
    }

    policy->fw_major = value;
}

static void
qsev_policy_get_fw_minor(Object *obj, Visitor *v,
                         const char *name, void *opaque,
                         Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);
    uint8_t value = policy->fw_minor;

    visit_type_uint8(v, name, &value, errp);
}

static void
qsev_policy_set_fw_minor(Object *obj, Visitor *v,
                         const char *name, void *opaque,
                         Error **errp)
{
    QSevPolicyInfo *policy = QSEV_POLICY_INFO(obj);
    Error *error = NULL;
    uint8_t value;

    visit_type_uint8(v, name, &value, &error);
    if (error) {
        return;
    }

    policy->fw_minor = value;
}

static void
qsev_policy_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_bool(oc, "debug",
                                   qsev_policy_get_debug,
                                   qsev_policy_set_debug,
                                   NULL);
    object_class_property_set_description(oc, "debug",
            "Set on/off if debugging is allowed on this guest",
            NULL);

    object_class_property_add_bool(oc, "ks",
                                   qsev_policy_get_ks,
                                   qsev_policy_set_ks,
                                   NULL);
    object_class_property_set_description(oc, "ks",
            "Set on/off if guest is allowed to share key with others.",
            NULL);

    object_class_property_add_bool(oc, "nosend",
                                   qsev_policy_get_nosend,
                                   qsev_policy_set_nosend,
                                   NULL);
    object_class_property_set_description(oc, "nosend",
            "Set on/off if sending guest to anoter platform is allowed",
            NULL);

    object_class_property_add_bool(oc, "domain",
                                   qsev_policy_get_domain,
                                   qsev_policy_set_domain,
                                   NULL);
    object_class_property_set_description(oc, "domain",
            "Set on/off if guest should not be transmitted to another platform that is not in the same domain.",
            NULL);

    object_class_property_add_bool(oc, "sev",
                                   qsev_policy_get_sev,
                                   qsev_policy_set_sev,
                                   NULL);
    object_class_property_set_description(oc, "domain",
            "Set on/off if guest should not be transmitted to another non SEV platform",
            NULL);

    object_class_property_add(oc, "fw_major", "uint8",
                                   qsev_policy_get_fw_major,
                                   qsev_policy_set_fw_major,
                                   NULL, NULL, NULL);
    object_class_property_set_description(oc, "fw_major",
            "guest must not be transmitted to another platform with a lower firmware version",
            NULL);
    object_class_property_add(oc, "fw_minor", "uint8",
                                   qsev_policy_get_fw_minor,
                                   qsev_policy_set_fw_minor,
                                   NULL, NULL, NULL);
    object_class_property_set_description(oc, "fw_minor",
            "guest must not be transmitted to another platform with a lower firmware version",
            NULL);
}

static void
qsev_policy_finalize(Object *obj)
{
}

static QSevPolicyInfo *
lookup_sev_policy_info(const char *id)
{
    QSevPolicyInfo *policy;
    Object *obj = get_object_by_id(id);

    if (!obj) {
        return NULL;
    }

    policy = (QSevPolicyInfo *)
        object_dynamic_cast(obj,
                            TYPE_QSEV_POLICY_INFO);
    if (!policy) {
        return NULL;
    }

    return policy;
}

static uint32_t
sev_policy_get_value(const char *id)
{
    uint32_t val = 0;
    QSevPolicyInfo *policy = lookup_sev_policy_info(id);

    if (!policy) {
        return 0;
    }

    val = policy->debug;
    val |= policy->ks << 1;
    val |= (1 << 2);
    val |= policy->nosend << 3;
    val |= policy->domain << 4;
    val |= policy->sev << 5;
    val |= policy->fw_major << 16;
    val |= policy->fw_minor << 24;

    return val;
}

/* qsev policy */
static const TypeInfo qsev_policy_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QSEV_POLICY_INFO,
    .instance_size = sizeof(QSevPolicyInfo),
    .instance_finalize = qsev_policy_finalize,
    .class_size = sizeof(QSevPolicyInfoClass),
    .class_init = qsev_policy_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static char *
qsev_guest_get_launch_id(Object *obj, Error **errp)
{
    QSevGuestInfo *sev_info = QSEV_GUEST_INFO(obj);

    return g_strdup(sev_info->launch);
}

static void
qsev_guest_set_launch_id(Object *obj, const char *value, Error **errp)
{
    QSevGuestInfo *sev_info = QSEV_GUEST_INFO(obj);

    sev_info->launch = g_strdup(value);
}

static char *
qsev_guest_get_send_id(Object *obj, Error **errp)
{
    QSevGuestInfo *sev_info = QSEV_GUEST_INFO(obj);

    return g_strdup(sev_info->send);
}

static void
qsev_guest_set_send_id(Object *obj, const char *value, Error **errp)
{
    QSevGuestInfo *sev_info = QSEV_GUEST_INFO(obj);

    sev_info->send = g_strdup(value);
}

static void
qsev_guest_finalize(Object *obj)
{
    QSevGuestInfo *sev_info = QSEV_GUEST_INFO(obj);

    g_free(sev_info->launch);
    g_free(sev_info->send);
}

static void
qsev_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "launch",
                                  qsev_guest_get_launch_id,
                                  qsev_guest_set_launch_id,
                                  NULL);
    object_class_property_set_description(oc, "launch",
            "Set the launch object id to use", NULL);
    object_class_property_add_str(oc, "send",
                                  qsev_guest_get_send_id,
                                  qsev_guest_set_send_id,
                                  NULL);
    object_class_property_set_description(oc, "send",
            "Set the send object id to use when migrating the guest", NULL);
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

static uint8_t
sev_guest_info_get_mode(const char *id)
{
    uint8_t ret = SEV_LAUNCH_INVALID;
    Object *obj;

    obj = get_object_by_id(id);
    if (object_dynamic_cast(obj, TYPE_QSEV_LAUNCH_INFO)) {
        ret = SEV_LAUNCH_UNENCRYPTED;
    } else if (object_dynamic_cast(obj, TYPE_QSEV_RECEIVE_INFO)) {
        ret = SEV_LAUNCH_ENCRYPTED;
    }

    return ret;
}

/* sev guest info */
static const TypeInfo qsev_guest_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QSEV_GUEST_INFO,
    .instance_size = sizeof(QSevGuestInfo),
    .instance_finalize = qsev_guest_finalize,
    .class_size = sizeof(QSevGuestInfoClass),
    .class_init = qsev_guest_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
qsev_launch_finalize(Object *obj)
{
}

static char *
qsev_launch_get_policy_id(Object *obj, Error **errp)
{
    QSevLaunchInfo *sev_info = QSEV_LAUNCH_INFO(obj);

    return g_strdup(sev_info->policy_id);
}

static void
qsev_launch_set_policy_id(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *sev_info = QSEV_LAUNCH_INFO(obj);

    sev_info->policy_id = g_strdup(value);
}

static bool
qsev_launch_get_flags_ks(Object *obj, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    return launch->flags_ks;
}

static void
qsev_launch_set_flags_ks(Object *obj, bool value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    launch->flags_ks = value;
}

static char *
qsev_launch_get_nonce(Object *obj, Error **errp)
{
    char *value, *str;
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    str = uint8_ptr_to_str(launch->nonce, sizeof(launch->nonce));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_launch_set_nonce(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    str_to_uint8_ptr(value, launch->nonce, sizeof(launch->nonce));
}

static char *
qsev_launch_get_dh_pub_qx(Object *obj, Error **errp)
{
    char *value, *str;
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    str = uint8_ptr_to_str(launch->dh_pub_qx, sizeof(launch->dh_pub_qx));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_launch_set_dh_pub_qx(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    str_to_uint8_ptr(value, launch->dh_pub_qx,
                     sizeof(launch->dh_pub_qx));
}

static char *
qsev_launch_get_dh_pub_qy(Object *obj, Error **errp)
{
    char *value, *str;
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    str = uint8_ptr_to_str(launch->dh_pub_qy, sizeof(launch->dh_pub_qy));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_launch_set_dh_pub_qy(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    str_to_uint8_ptr(value, launch->dh_pub_qy,
                     sizeof(launch->dh_pub_qy));
}

static void
qsev_launch_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_bool(oc, "flags.ks",
                                   qsev_launch_get_flags_ks,
                                   qsev_launch_set_flags_ks,
                                   NULL);
    object_class_property_set_description(oc, "flags.ks",
            "Set on/off if key sharing with other guests is allowed",
            NULL);

    object_class_property_add_str(oc, "policy",
                                  qsev_launch_get_policy_id,
                                  qsev_launch_set_policy_id,
                                  NULL);
    object_class_property_set_description(oc, "policy",
            "Set the guest owner's sev-policy id", NULL);

    object_class_property_add_str(oc, "nonce",
                                  qsev_launch_get_nonce,
                                  qsev_launch_set_nonce,
                                  NULL);
    object_class_property_set_description(oc, "nonce",
            "a nonce provided by guest owner", NULL);

    object_class_property_add_str(oc, "dh-pub-qx",
                                  qsev_launch_get_dh_pub_qx,
                                  qsev_launch_set_dh_pub_qx,
                                  NULL);
    object_class_property_set_description(oc, "dh-pub-qx",
            "Qx parameter of owner's ECDH public key", NULL);

    object_class_property_add_str(oc, "dh-pub-qy",
                                  qsev_launch_get_dh_pub_qy,
                                  qsev_launch_set_dh_pub_qy,
                                  NULL);
    object_class_property_set_description(oc, "dh-pub-qy",
            "Qy parameter of owner's ECDH public key", NULL);
}

static uint8_t
sev_launch_info_get_flags(QSevLaunchInfo *launch)
{
    uint8_t flags = launch->flags_ks;

    return flags;
}

static QSevLaunchInfo *
lookup_sev_launch_info(const char *id)
{
    Object *obj;
    QSevLaunchInfo *info;

    obj = object_resolve_path_component(
        object_get_objects_root(), id);
    if (!obj) {
        return NULL;
    }

    info = (QSevLaunchInfo *)
            object_dynamic_cast(obj, TYPE_QSEV_LAUNCH_INFO);
    if (!info) {
        return NULL;
    }

    return info;
}

static int
sev_launch_info_get_params(const char *id,
                           struct kvm_sev_launch_start **s,
                           struct kvm_sev_launch_update **u,
                           struct kvm_sev_launch_finish **f)
{
    QSevLaunchInfo *info;
    struct kvm_sev_launch_start *start;
    struct kvm_sev_launch_finish *finish;

    info = lookup_sev_launch_info(id);
    if (!info) {
        return -1;
    }

    start = g_malloc0(sizeof(*start));
    start->flags = sev_launch_info_get_flags(info);
    start->policy = sev_policy_get_value(info->policy_id);
    memcpy(start->nonce, info->nonce, sizeof(info->nonce));
    memcpy(start->dh_pub_qx, info->dh_pub_qx, sizeof(info->dh_pub_qx));
    memcpy(start->dh_pub_qy, info->dh_pub_qy, sizeof(info->dh_pub_qy));

    finish = g_malloc0(sizeof(*finish));

    DPRINTF("sev-launch\n");
    DPRINTF(" flags: %#x\n", start->flags);
    DPRINTF(" policy: %#x\n", start->policy);
    DPRINTF_U8_PTR(" dh_pub_qx", start->dh_pub_qx, sizeof(start->dh_pub_qx));
    DPRINTF_U8_PTR(" dh_pub_qy", start->dh_pub_qy, sizeof(start->dh_pub_qy));
    DPRINTF_U8_PTR(" nonce", start->nonce, sizeof(start->nonce));

    *s = start;
    *u = g_malloc(sizeof(struct kvm_sev_launch_update));
    *f = finish;

    return 0;
}

/* unencrypted guest launch */
static const TypeInfo qsev_launch_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QSEV_LAUNCH_INFO,
    .instance_size = sizeof(QSevLaunchInfo),
    .instance_finalize = qsev_launch_finalize,
    .class_size = sizeof(QSevLaunchInfoClass),
    .class_init = qsev_launch_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
qsev_receive_finalize(Object *obj)
{
}

static char *
qsev_receive_get_policy_id(Object *obj, Error **errp)
{
    QSevReceiveInfo *sev_info = QSEV_RECEIVE_INFO(obj);

    return g_strdup(sev_info->policy_id);
}

static void
qsev_receive_set_policy_id(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *sev_info = QSEV_RECEIVE_INFO(obj);

    sev_info->policy_id = g_strdup(value);
}

static bool
qsev_receive_get_flags_ks(Object *obj, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    return receive->flags_ks;
}

static void
qsev_receive_set_flags_ks(Object *obj, bool value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    receive->flags_ks = value;
}

static char *
qsev_receive_get_nonce(Object *obj, Error **errp)
{
    char *value, *str;
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str = uint8_ptr_to_str(receive->nonce, sizeof(receive->nonce));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_receive_set_nonce(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str_to_uint8_ptr(value, receive->nonce, sizeof(receive->nonce));
}

static char *
qsev_receive_get_dh_pub_qx(Object *obj, Error **errp)
{
    char *value, *str;
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str = uint8_ptr_to_str(receive->dh_pub_qx, sizeof(receive->dh_pub_qx));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_receive_set_dh_pub_qx(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str_to_uint8_ptr(value, receive->dh_pub_qx,
                     sizeof(receive->dh_pub_qx));
}

static char *
qsev_receive_get_dh_pub_qy(Object *obj, Error **errp)
{
    char *value, *str;
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str = uint8_ptr_to_str(receive->dh_pub_qy, sizeof(receive->dh_pub_qy));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_receive_set_dh_pub_qy(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str_to_uint8_ptr(value, receive->dh_pub_qy,
                     sizeof(receive->dh_pub_qy));
}

static char *
qsev_receive_get_ten(Object *obj, Error **errp)
{
    char *value, *str;
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str = uint8_ptr_to_str(receive->ten, sizeof(receive->ten));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_receive_set_ten(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str_to_uint8_ptr(value, receive->ten,
                     sizeof(receive->ten));
}

static char *
qsev_receive_get_wrapped_tik(Object *obj, Error **errp)
{
    char *value, *str;
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str = uint8_ptr_to_str(receive->wrapped_tik, sizeof(receive->wrapped_tik));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_receive_set_wrapped_tik(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str_to_uint8_ptr(value, receive->wrapped_tik,
                     sizeof(receive->wrapped_tik));
}

static char *
qsev_receive_get_wrapped_tek(Object *obj, Error **errp)
{
    char *value, *str;
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str = uint8_ptr_to_str(receive->wrapped_tek, sizeof(receive->wrapped_tek));
    value = g_strdup(str);
    g_free(str);

    return value;
}

static void
qsev_receive_set_wrapped_tek(Object *obj, const char *value, Error **errp)
{
    QSevReceiveInfo *receive = QSEV_RECEIVE_INFO(obj);

    str_to_uint8_ptr(value, receive->wrapped_tek,
                     sizeof(receive->wrapped_tek));
}

static void
qsev_receive_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_bool(oc, "flags.ks",
                                   qsev_receive_get_flags_ks,
                                   qsev_receive_set_flags_ks,
                                   NULL);
    object_class_property_set_description(oc, "flags.ks",
            "Set on/off if key sharing with other guests is allowed",
            NULL);

    object_class_property_add_str(oc, "policy",
                                  qsev_receive_get_policy_id,
                                  qsev_receive_set_policy_id,
                                  NULL);
    object_class_property_set_description(oc, "policy",
            "Set the guest origin sev-policy id", NULL);

    object_class_property_add_str(oc, "nonce",
                                  qsev_receive_get_nonce,
                                  qsev_receive_set_nonce,
                                  NULL);
    object_class_property_set_description(oc, "nonce",
            "a nonce provided by origin", NULL);

    object_class_property_add_str(oc, "dh-pub-qx",
                                  qsev_receive_get_dh_pub_qx,
                                  qsev_receive_set_dh_pub_qx,
                                  NULL);
    object_class_property_set_description(oc, "dh-pub-qx",
            "Qx parameter of origin ECDH public key", NULL);

    object_class_property_add_str(oc, "dh-pub-qy",
                                  qsev_receive_get_dh_pub_qy,
                                  qsev_receive_set_dh_pub_qy,
                                  NULL);
    object_class_property_set_description(oc, "dh-pub-qy",
            "Qy parameter of origin ECDH public key", NULL);

    object_class_property_add_str(oc, "ten",
                                  qsev_receive_get_ten,
                                  qsev_receive_set_ten,
                                  NULL);
    object_class_property_set_description(oc, "ten",
            "Set transport encryption nonce", NULL);

    object_class_property_add_str(oc, "wrapped-tik",
                                  qsev_receive_get_wrapped_tik,
                                  qsev_receive_set_wrapped_tik,
                                  NULL);
    object_class_property_set_description(oc, "wrapped-tik",
            "The wrapped transport identity key", NULL);

    object_class_property_add_str(oc, "wrapped-tek",
                                  qsev_receive_get_wrapped_tek,
                                  qsev_receive_set_wrapped_tek,
                                  NULL);
    object_class_property_set_description(oc, "wrapped-tek",
            "The wrapped transport encryption key", NULL);
}

/* pre-encrypted guest launch */
static const TypeInfo qsev_receive_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QSEV_RECEIVE_INFO,
    .instance_size = sizeof(QSevReceiveInfo),
    .instance_finalize = qsev_receive_finalize,
    .class_size = sizeof(QSevReceiveInfoClass),
    .class_init = qsev_receive_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static int
sev_ioctl(int cmd, void *data)
{
    int ret;
    struct kvm_sev_issue_cmd input;

    input.cmd = cmd;
    input.opaque = (__u64)data;
    ret = kvm_vm_ioctl(kvm_state, KVM_SEV_ISSUE_CMD, &input);
    if (ret) {
        fprintf(stderr, "sev_ioctl failed cmd=%#x, ret=%d(%#010x)\n",
                cmd, ret, input.ret_code);
        return ret;
    }

    return 0;
}

static int
sev_launch_start(SEVState *s)
{
    int ret;

    if (s->state == SEV_STATE_LAUNCHING) {
        return 0;
    }

    ret = sev_launch_info_get_params(s->launch_id, &s->launch_start,
                                     &s->launch_update, &s->launch_finish);
    if (ret < 0) {
        return -1;
    }

    ret = sev_ioctl(KVM_SEV_LAUNCH_START, s->launch_start);
    if (ret < 0) {
        return -1;
    }

    s->state = SEV_STATE_LAUNCHING;
    DPRINTF("SEV: LAUNCH_START\n");
    return 0;
}

static int
sev_launch_finish(SEVState *s)
{
    int ret;
    struct kvm_sev_launch_finish *finish = s->launch_finish;

    assert(s->state == SEV_STATE_LAUNCHING);

    ret = sev_ioctl(KVM_SEV_LAUNCH_FINISH, finish);
    if (ret) {
        return -1;
    }

    DPRINTF("SEV: LAUNCH_FINISH ");
    DPRINTF_U8_PTR(" measurement", finish->measurement,
                   sizeof(finish->measurement));

    s->state = SEV_STATE_RUNNING;
    return 0;
}

static int
sev_launch_update(SEVState *s, uint8_t *addr, uint32_t len)
{
    int ret;
    struct kvm_sev_launch_update *data = s->launch_update;

    assert(s->state == SEV_STATE_LAUNCHING);
    data->address = (__u64)addr;
    data->length = len;

    ret = sev_ioctl(KVM_SEV_LAUNCH_UPDATE, data);
    if (ret) {
        return ret;
    }

    DPRINTF("SEV: LAUNCH_UPDATE %#lx+%#x\n", (unsigned long)addr, len);
    return 0;
}

/**
 * Function returns 'true' if id is a valid QSevGuestInfo object.
 */
bool
has_sev_guest_policy(const char *id)
{
    return lookup_sev_guest_info(id) ? true : false;
}

void *
sev_guest_init(const char *id)
{
    int ret;
    SEVState *s;
    QSevGuestInfo *sev_info;

    s = g_malloc0(sizeof(SEVState));
    if (!s) {
        return NULL;
    }

    sev_info = lookup_sev_guest_info(id);
    if (!sev_info) {
        fprintf(stderr, "'%s' not a valid '%s' object\n",
                id, TYPE_QSEV_GUEST_INFO);
        goto err;
    }

    s->mode = sev_guest_info_get_mode(sev_info->launch);
    if (s->mode == SEV_LAUNCH_INVALID) {
        fprintf(stderr, "'%s' invalid sev launch id\n", sev_info->launch);
        goto err;
    }

    s->sev_info_id = g_strdup(id);
    s->launch_id = g_strdup(sev_info->launch);

    /* now launch the guest */
    ret = sev_guest_launch_start(s);
    if (ret < 0) {
        goto err;
    }

    sev_allowed = true;
    return s;
err:
    g_free(s);

    return NULL;
}

int
sev_guest_launch_start(void *handle)
{
    SEVState *s = (SEVState *)handle;

    if (s->state == SEV_STATE_RUNNING) {
        return 0;
    }

    if (s->mode == SEV_LAUNCH_UNENCRYPTED) {
        return sev_launch_start(s);
    } else if (s->mode == SEV_LAUNCH_ENCRYPTED) {
        // use receive_info commands
    }

    return -1;
}

int
sev_guest_launch_finish(void *handle)
{
    SEVState *s = (SEVState *)handle;

    if (s->state == SEV_STATE_RUNNING) {
        return 0;
    }

    if (s->state == SEV_STATE_LAUNCHING) {
        return sev_launch_finish(s);
    } else if (s->state == SEV_STATE_RECEIVING) {
        // use receive_finish commands
    } else {
        return -1;
    }

    return -1;
}

static int
sev_debug_decrypt(SEVState *s, uint8_t *dst, const uint8_t *src, uint32_t len)
{
    int ret;
    struct kvm_sev_dbg_decrypt *dbg;

    dbg = g_malloc0(sizeof(*dbg));
    dbg->src_addr = (unsigned long)src;
    dbg->dst_addr = (unsigned long)dst;
    dbg->length = len;

    ret = sev_ioctl(KVM_SEV_DBG_DECRYPT, dbg);
    DPRINTF("SEV: DBG_DECRYPT src %#lx dst %#lx len %#x\n",
            (uint64_t)src, (uint64_t)dst, len);
    g_free(dbg);
    return ret;
}

static int
sev_mem_write(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    SEVState *s = kvm_memory_encryption_get_handle();

    assert(s != NULL);

    if (s->state == SEV_STATE_LAUNCHING) {
        memcpy(dst, src, len);
        return sev_launch_update(s, dst, len);
    }

    return 0;
}

static int
sev_mem_read(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    SEVState *s = kvm_memory_encryption_get_handle();

    assert(s != NULL);
    assert(attrs.debug || s->state != SEV_STATE_RUNNING);

    return sev_debug_decrypt(s, dst, src, len);
}

void
sev_guest_set_ops(void *handle, MemoryRegion *mr)
{
    SEVState *s = (SEVState *)handle;

    assert(s != NULL);

    sev_ops.read = sev_mem_read;
    sev_ops.write = sev_mem_write;

    memory_region_set_ram_ops(mr, &sev_ops);
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
    type_register_static(&qsev_policy_info);
    type_register_static(&qsev_launch_info);
    type_register_static(&qsev_receive_info);
}

type_init(sev_policy_register_types);
