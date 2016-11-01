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

static MemoryRegionRAMReadWriteOps sev_ops;
static bool sev_allowed;

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

static void
DPRINTF_U8_PTR(const char *name, const uint8_t *ptr, int count)
{
    int i;

    DPRINTF("%s = ", name);
    for (i = 0; i < count; i++) {
        DPRINTF("%02hhx", ptr[i]);
    }
    DPRINTF("\n");
}

static void
qsev_guest_finalize(Object *obj)
{
}

static void
qsev_guest_class_init(ObjectClass *oc, void *data)
{
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

static char *
qsev_launch_get_nonce(Object *obj, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    return g_strdup(launch->nonce);
}

static void
qsev_launch_set_nonce(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    launch->nonce = g_strdup(value);
}

static char *
qsev_launch_get_dh_pub_qx(Object *obj, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    return g_strdup(launch->dh_pub_qx);
}

static void
qsev_launch_set_dh_pub_qx(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    launch->dh_pub_qx = g_strdup(value);
}

static char *
qsev_launch_get_dh_pub_qy(Object *obj, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    return g_strdup(launch->dh_pub_qy);
}

static void
qsev_launch_set_dh_pub_qy(Object *obj, const char *value, Error **errp)
{
    QSevLaunchInfo *launch = QSEV_LAUNCH_INFO(obj);

    launch->dh_pub_qy = g_strdup(value);
}

static void
qsev_launch_class_init(ObjectClass *oc, void *data)
{
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

static void
get_sev_property_ptr(Object *obj, const char *name, uint8_t *ptr, int count)
{
    char *value;

    value = object_property_get_str(obj, name, &error_abort);
    str_to_uint8_ptr(value, ptr, count);
    DPRINTF_U8_PTR(name, ptr, count);
    g_free(value);
}

static int
sev_launch_start(SEVState *s)
{
    int ret;
    Object *obj;
    struct kvm_sev_launch_start *start;

    if (s->state == SEV_STATE_LAUNCHING) {
        return 0;
    }

    start = g_malloc0(sizeof(*start));
    if (!start) {
        return 1;
    }

    obj = object_property_get_link(OBJECT(s->sev_info), "launch", &error_abort);
    get_sev_property_ptr(obj, "dh-pub-qx", start->dh_pub_qx,
            sizeof(start->dh_pub_qx));
    get_sev_property_ptr(obj, "dh-pub-qy", start->dh_pub_qy,
            sizeof(start->dh_pub_qy));
    get_sev_property_ptr(obj, "nonce", start->nonce, sizeof(start->nonce));
    ret = sev_ioctl(KVM_SEV_LAUNCH_START, start);
    if (ret < 0) {
        return 1;
    }

    s->state = SEV_STATE_LAUNCHING;
    g_free(start);

    DPRINTF("SEV: LAUNCH_START\n");
    return 0;
}

static int
sev_launch_finish(SEVState *s)
{
    int ret;
    struct kvm_sev_launch_finish *data;

    assert(s->state == SEV_STATE_LAUNCHING);

    data = g_malloc0(sizeof(*data));
    if (!data) {
        return 1;
    }

    ret = sev_ioctl(KVM_SEV_LAUNCH_FINISH, data);
    if (ret) {
        goto err;
    }

    DPRINTF("SEV: LAUNCH_FINISH ");
    DPRINTF_U8_PTR(" measurement", data->measurement,
                   sizeof(data->measurement));

    s->state = SEV_STATE_RUNNING;
err:
    g_free(data);

    return ret;
}

static int
sev_launch_update(SEVState *s, uint8_t *addr, uint32_t len)
{
    int ret;
    struct kvm_sev_launch_update *data;

    data = g_malloc0(sizeof(*data));
    if (!data) {
        return 1;
    }

    data->address = (__u64)addr;
    data->length = len;
    ret = sev_ioctl(KVM_SEV_LAUNCH_UPDATE, data);
    if (ret) {
        goto err;
    }

    DPRINTF("SEV: LAUNCH_UPDATE %#lx+%#x\n", (unsigned long)addr, len);
err:
    g_free(data);
    return ret;
}

static int
sev_debug_decrypt(SEVState *s, uint8_t *dst, const uint8_t *src, uint32_t len)
{
    int ret;
    struct kvm_sev_dbg_decrypt *dbg;

    dbg = g_malloc0(sizeof(*dbg));
    if (!dbg) {
        return 1;
    }

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
sev_debug_encrypt(SEVState *s, uint8_t *dst, const uint8_t *src, uint32_t len)
{
    int ret;
    struct kvm_sev_dbg_encrypt *dbg;

    dbg = g_malloc0(sizeof(*dbg));
    if (!dbg) {
        return 1;
    }

    dbg->src_addr = (unsigned long)src;
    dbg->dst_addr = (unsigned long)dst;
    dbg->length = len;

    ret = sev_ioctl(KVM_SEV_DBG_ENCRYPT, dbg);
    DPRINTF("SEV: debug encrypt src %#lx dst %#lx len %#x\n",
            (unsigned long)src, (unsigned long)dst, len);
    g_free(dbg);

    return ret;
}

static int
sev_mem_write(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    SEVState *s = kvm_memory_encryption_get_handle();

    assert(attrs.debug || (s != NULL && s->state != SEV_STATE_INVALID));

    if (s->state == SEV_STATE_LAUNCHING) {
        memcpy(dst, src, len);
        return sev_launch_update(s, dst, len);
    }

    return sev_debug_encrypt(s, dst, src, len);
}

static int
sev_mem_read(uint8_t *dst, const uint8_t *src, uint32_t len, MemTxAttrs attrs)
{
    SEVState *s = kvm_memory_encryption_get_handle();

    assert(attrs.debug || (s != NULL && s->state != SEV_STATE_INVALID));

    return sev_debug_decrypt(s, dst, src, len);
}

static int
sev_get_launch_type(SEVState *s)
{
    QSevGuestInfo *sev_info = s->sev_info;

    /* if <link>QSevLaunchInfo is set then we are configured to use
     * launch_info object.
     */
    if (object_property_get_link(OBJECT(sev_info), "launch", &error_abort)) {
        return USE_LAUNCH_INFO;
    }

    return INVALID_TYPE;
}

void *
sev_guest_init(const char *id)
{
    Object *obj;
    SEVState *s;

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

int
sev_guest_launch_start(void *handle)
{
    SEVState *s = (SEVState *)handle;

    assert(s != NULL);

    /* If we are in prelaunch state then create memory encryption context based
     * on the sev launch object created by user.
     */
    if (runstate_check(RUN_STATE_PRELAUNCH)) {
        if (sev_get_launch_type(s) == USE_LAUNCH_INFO) {
            return sev_launch_start(s);
        }
    }

    return 1;
}

int
sev_guest_launch_finish(void *handle)
{
    SEVState *s = (SEVState *)handle;

    assert(s != NULL);

    if (s->state == SEV_STATE_LAUNCHING) {
        return sev_launch_finish(s);
    }

    return 1;
}

void
sev_guest_set_debug_ops(void *handle, MemoryRegion *mr)
{
    SEVState *s = (SEVState *)handle;

    assert(s != NULL);

    sev_ops.read = sev_mem_read;
    sev_ops.write = sev_mem_write;

    memory_region_set_ram_debug_ops(mr, &sev_ops);
}

int
sev_guest_mem_dec(void *handle, uint8_t *dst, const uint8_t *src, uint32_t len)
{
    SEVState *s = (SEVState *)handle;

    assert(s != NULL && s->state != SEV_STATE_INVALID);

    /* use SEV debug command to decrypt memory */
    return sev_debug_decrypt((SEVState *)handle, dst, src, len);
}

int
sev_guest_mem_enc(void *handle, uint8_t *dst, const uint8_t *src, uint32_t len)
{
    SEVState *s = (SEVState *)handle;

    assert(s != NULL && s->state != SEV_STATE_INVALID);

    /* use SEV debug command to decrypt memory */
    return sev_debug_encrypt((SEVState *)handle, dst, src, len);
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
