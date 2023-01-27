/*
 * QEMU Arm RME support
 *
 * Copyright Linaro 2022
 */

#include "qemu/osdep.h"

#include "exec/confidential-guest-support.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "hw/loader.h"
#include "kvm_arm.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"

#define TYPE_RME_GUEST "rme-guest"
OBJECT_DECLARE_SIMPLE_TYPE(RmeGuest, RME_GUEST)

#define RME_PAGE_SIZE qemu_real_host_page_size()

#define RME_MAX_CFG         1

typedef struct RmeGuest RmeGuest;

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
    char *measurement_algo;
};

struct RmeImage {
    hwaddr base;
    hwaddr src_size;
    hwaddr dst_size;
};

static GSList *rme_images;

static RmeGuest *cgs_to_rme(ConfidentialGuestSupport *cgs)
{
    if (!cgs) {
        return NULL;
    }
    return (RmeGuest *)object_dynamic_cast(OBJECT(cgs), TYPE_RME_GUEST);
}

bool kvm_arm_rme_enabled(void)
{
    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;

    return !!cgs_to_rme(cgs);
}

static int rme_create_rd(RmeGuest *guest, Error **errp)
{
    int ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                                KVM_CAP_ARM_RME_CREATE_RD);

    if (ret) {
        error_setg_errno(errp, -ret, "RME: failed to create Realm Descriptor");
    }
    return ret;
}

static int rme_configure_one(RmeGuest *guest, uint32_t cfg, Error **errp)
{
    int ret;
    const char *cfg_str;
    struct kvm_cap_arm_rme_config_item args = {
        .cfg = cfg,
    };

    switch (cfg) {
    case KVM_CAP_ARM_RME_CFG_HASH_ALGO:
        if (!guest->measurement_algo) {
            return 0;
        }
        if (!strcmp(guest->measurement_algo, "sha256")) {
            args.hash_algo = KVM_CAP_ARM_RME_MEASUREMENT_ALGO_SHA256;
        } else if (!strcmp(guest->measurement_algo, "sha512")) {
            args.hash_algo = KVM_CAP_ARM_RME_MEASUREMENT_ALGO_SHA512;
        } else {
            g_assert_not_reached();
        }
        cfg_str = "hash algorithm";
        break;
    default:
        g_assert_not_reached();
    }

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                            KVM_CAP_ARM_RME_CONFIG_REALM, (intptr_t)&args);
    if (ret) {
        error_setg_errno(errp, -ret, "RME: failed to configure %s", cfg_str);
    }
    return ret;
}

static void rme_populate_realm(gpointer data, gpointer user_data)
{
    int ret;
    struct RmeImage *image = data;
    struct kvm_cap_arm_rme_init_ipa_args init_args = {
        .init_ipa_base = image->base,
        .init_ipa_size = image->dst_size,
    };
    struct kvm_cap_arm_rme_populate_realm_args populate_args = {
        .populate_ipa_base = image->base,
        .populate_ipa_size = image->src_size,
    };

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                            KVM_CAP_ARM_RME_INIT_IPA_REALM,
                            (intptr_t)&init_args);
    if (ret) {
        error_setg_errno(&error_fatal, -ret,
                         "RME: failed to initialize GPA range (0x%"HWADDR_PRIx", 0x%"HWADDR_PRIx")",
                         image->base, image->dst_size);
    }

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                            KVM_CAP_ARM_RME_POPULATE_REALM,
                            (intptr_t)&populate_args);
    if (ret) {
        error_setg_errno(&error_fatal, -ret,
                         "RME: failed to populate realm (0x%"HWADDR_PRIx", 0x%"HWADDR_PRIx")",
                         image->base, image->src_size);
    }
}

static void rme_vm_state_change(void *opaque, bool running, RunState state)
{
    int ret;
    CPUState *cs;

    if (state != RUN_STATE_RUNNING) {
        return;
    }

    /*
     * Now that do_cpu_reset() initialized the boot PC and
     * kvm_cpu_synchronize_post_reset() registered it, we can finalize the REC.
     */
    CPU_FOREACH(cs) {
        ret = kvm_arm_vcpu_finalize(cs, KVM_ARM_VCPU_REC);
        if (ret) {
            error_setg_errno(&error_fatal, -ret,
                             "RME: failed to finalize vCPU");
        }
    }

    g_slist_foreach(rme_images, rme_populate_realm, NULL);
    g_slist_free_full(g_steal_pointer(&rme_images), g_free);

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                            KVM_CAP_ARM_RME_ACTIVATE_REALM);
    if (ret) {
        error_setg_errno(&error_fatal, -ret, "RME: failed to activate realm");
    }
}

int kvm_arm_rme_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    int ret;
    int cfg;
    static Error *rme_mig_blocker;
    RmeGuest *guest = cgs_to_rme(cgs);

    if (!guest) {
        /* Either no cgs, or another confidential guest type */
        return 0;
    }

    if (!kvm_enabled()) {
        error_setg(errp, "KVM required for RME");
        return -ENODEV;
    }

    if (!kvm_check_extension(kvm_state, KVM_CAP_ARM_RME)) {
        error_setg(errp, "KVM does not support RME");
        return -ENODEV;
    }

    for (cfg = 0; cfg < RME_MAX_CFG; cfg++) {
        ret = rme_configure_one(guest, cfg, errp);
        if (ret) {
            return ret;
        }
    }

    ret = rme_create_rd(guest, errp);
    if (ret) {
        return ret;
    }

    error_setg(&rme_mig_blocker, "RME: migration is not implemented");
    migrate_add_blocker(rme_mig_blocker, &error_fatal);

    /*
     * The realm activation is done last, when the VM starts, after all images
     * have been loaded and all vcpus finalized.
     */
    qemu_add_vm_change_state_handler(rme_vm_state_change, guest);

    cgs->ready = true;
    return 0;
}

/*
 * kvm_arm_rme_add_blob - Initialize the Realm IPA range and set up the image.
 *
 * @src_size is the number of bytes of the source image, to be populated into
 *   Realm memory.
 * @dst_size is the effective image size, which may be larger than @src_size.
 *   For a kernel @dst_size may include zero-initialized regions such as the BSS
 *   and initial page directory.
 */
void kvm_arm_rme_add_blob(hwaddr base, hwaddr src_size, hwaddr dst_size)
{
    struct RmeImage *image;

    if (!kvm_arm_rme_enabled()) {
        return;
    }

    base = QEMU_ALIGN_DOWN(base, RME_PAGE_SIZE);
    src_size = QEMU_ALIGN_UP(src_size, RME_PAGE_SIZE);
    dst_size = QEMU_ALIGN_UP(dst_size, RME_PAGE_SIZE);

    image = g_malloc0(sizeof(*image));
    image->base = base;
    image->src_size = src_size;
    image->dst_size = dst_size;

    /*
     * The ROM loader will only load the images during reset, so postpone the
     * populate call until VM start.
     */
    rme_images = g_slist_prepend(rme_images, image);
}

int kvm_arm_rme_vcpu_init(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (kvm_arm_rme_enabled()) {
        cpu->kvm_rme = true;
    }
    return 0;
}

int kvm_arm_rme_vm_type(MachineState *ms)
{
    if (cgs_to_rme(ms->cgs)) {
        return KVM_VM_TYPE_ARM_REALM;
    }
    return 0;
}

static char *rme_get_measurement_algo(Object *obj, Error **errp)
{
    RmeGuest *guest = RME_GUEST(obj);

    return g_strdup(guest->measurement_algo);
}

static void rme_set_measurement_algo(Object *obj, const char *value,
                                     Error **errp)
{
    RmeGuest *guest = RME_GUEST(obj);

    if (strncmp(value, "sha256", 6) &&
        strncmp(value, "sha512", 6)) {
        error_setg(errp, "invalid Realm measurement algorithm '%s'", value);
        return;
    }
    g_free(guest->measurement_algo);
    guest->measurement_algo = g_strdup(value);
}

static void rme_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "measurement-algo",
                                  rme_get_measurement_algo,
                                  rme_set_measurement_algo);
    object_class_property_set_description(oc, "measurement-algo",
            "Realm measurement algorithm ('sha256', 'sha512')");
}

static const TypeInfo rme_guest_info = {
    .parent = TYPE_CONFIDENTIAL_GUEST_SUPPORT,
    .name = TYPE_RME_GUEST,
    .instance_size = sizeof(struct RmeGuest),
    .class_init = rme_guest_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void rme_register_types(void)
{
    type_register_static(&rme_guest_info);
}

type_init(rme_register_types);
