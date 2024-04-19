/*
 * QEMU Arm RME support
 *
 * Copyright Linaro 2024
 */

#include "qemu/osdep.h"

#include "exec/confidential-guest-support.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "hw/loader.h"
#include "kvm_arm.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"

#define TYPE_RME_GUEST "rme-guest"
OBJECT_DECLARE_SIMPLE_TYPE(RmeGuest, RME_GUEST)

#define RME_PAGE_SIZE qemu_real_host_page_size()

#define RME_MAX_CFG         2

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
    Notifier rom_load_notifier;
    GSList *ram_regions;
    uint8_t *personalization_value;
    RmeGuestMeasurementAlgo measurement_algo;
};

typedef struct {
    hwaddr base;
    hwaddr len;
    /* Populate guest RAM with data, or only initialize the IPA range */
    bool populate;
} RmeRamRegion;

static RmeGuest *rme_guest;

bool kvm_arm_rme_enabled(void)
{
    return !!rme_guest;
}

static int rme_create_rd(Error **errp)
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
    case KVM_CAP_ARM_RME_CFG_RPV:
        if (!guest->personalization_value) {
            return 0;
        }
        memcpy(args.rpv, guest->personalization_value, KVM_CAP_ARM_RME_RPV_SIZE);
        cfg_str = "personalization value";
        break;
    case KVM_CAP_ARM_RME_CFG_HASH_ALGO:
        switch (guest->measurement_algo) {
        case RME_GUEST_MEASUREMENT_ALGO_SHA256:
            args.hash_algo = KVM_CAP_ARM_RME_MEASUREMENT_ALGO_SHA256;
            break;
        case RME_GUEST_MEASUREMENT_ALGO_SHA512:
            args.hash_algo = KVM_CAP_ARM_RME_MEASUREMENT_ALGO_SHA512;
            break;
        default:
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

static int rme_configure(void)
{
    int ret;
    int cfg;

    for (cfg = 0; cfg < RME_MAX_CFG; cfg++) {
        ret = rme_configure_one(rme_guest, cfg, &error_abort);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

static void rme_populate_realm(gpointer data, gpointer unused)
{
    int ret;
    const RmeRamRegion *region = data;

    if (region->populate) {
        struct kvm_cap_arm_rme_populate_realm_args populate_args = {
            .populate_ipa_base = region->base,
            .populate_ipa_size = region->len,
            .flags = KVM_ARM_RME_POPULATE_FLAGS_MEASURE,
        };
        ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                                KVM_CAP_ARM_RME_POPULATE_REALM,
                                (intptr_t)&populate_args);
        if (ret) {
            error_report("RME: failed to populate realm (0x%"HWADDR_PRIx", 0x%"HWADDR_PRIx"): %s",
                         region->base, region->len, strerror(-ret));
            exit(1);
        }
    } else {
        struct kvm_cap_arm_rme_init_ipa_args init_args = {
            .init_ipa_base = region->base,
            .init_ipa_size = region->len,
        };
        ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                                KVM_CAP_ARM_RME_INIT_IPA_REALM,
                                (intptr_t)&init_args);
        if (ret) {
            error_report("RME: failed to initialize GPA range (0x%"HWADDR_PRIx", 0x%"HWADDR_PRIx"): %s",
                         region->base, region->len, strerror(-ret));
            exit(1);
        }
    }
}

static void rme_vm_state_change(void *opaque, bool running, RunState state)
{
    int ret;
    CPUState *cs;

    if (!running) {
        return;
    }

    ret = rme_configure();
    if (ret) {
        return;
    }

    ret = rme_create_rd(&error_abort);
    if (ret) {
        return;
    }

    g_slist_foreach(rme_guest->ram_regions, rme_populate_realm, NULL);
    g_slist_free_full(g_steal_pointer(&rme_guest->ram_regions), g_free);

    /*
     * Now that do_cpu_reset() initialized the boot PC and
     * kvm_cpu_synchronize_post_reset() registered it, we can finalize the REC.
     */
    CPU_FOREACH(cs) {
        ret = kvm_arm_vcpu_finalize(ARM_CPU(cs), KVM_ARM_VCPU_REC);
        if (ret) {
            error_report("RME: failed to finalize vCPU: %s", strerror(-ret));
            exit(1);
        }
    }

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                            KVM_CAP_ARM_RME_ACTIVATE_REALM);
    if (ret) {
        error_report("RME: failed to activate realm: %s", strerror(-ret));
        exit(1);
    }
}

static gint rme_compare_ram_regions(gconstpointer a, gconstpointer b)
{
        const RmeRamRegion *ra = a;
        const RmeRamRegion *rb = b;

        g_assert(ra->base != rb->base);
        return ra->base < rb->base ? -1 : 1;
}

static void rme_add_ram_region(hwaddr base, hwaddr len, bool populate)
{
    RmeRamRegion *region;

    region = g_new0(RmeRamRegion, 1);
    region->base = QEMU_ALIGN_DOWN(base, RME_PAGE_SIZE);
    region->len = QEMU_ALIGN_UP(len, RME_PAGE_SIZE);
    region->populate = populate;

    /*
     * The Realm Initial Measurement (RIM) depends on the order in which we
     * initialize and populate the RAM regions. To help a verifier
     * independently calculate the RIM, sort regions by GPA.
     */
    rme_guest->ram_regions = g_slist_insert_sorted(rme_guest->ram_regions,
                                                   region,
                                                   rme_compare_ram_regions);
}

static void rme_rom_load_notify(Notifier *notifier, void *data)
{
    RomLoaderNotify *rom = data;

    if (rom->addr == -1) {
        /*
         * These blobs (ACPI tables) are not loaded into guest RAM at reset.
         * Instead the firmware will load them via fw_cfg and measure them
         * itself.
         */
        return;
    }
    rme_add_ram_region(rom->addr, rom->max_len, /* populate */ true);
}

int kvm_arm_rme_init(MachineState *ms)
{
    static Error *rme_mig_blocker;
    ConfidentialGuestSupport *cgs = ms->cgs;

    if (!rme_guest) {
        return 0;
    }

    if (!cgs) {
        error_report("missing -machine confidential-guest-support parameter");
        return -EINVAL;
    }

    if (!kvm_check_extension(kvm_state, KVM_CAP_ARM_RME)) {
        return -ENODEV;
    }

    error_setg(&rme_mig_blocker, "RME: migration is not implemented");
    migrate_add_blocker(&rme_mig_blocker, &error_fatal);

    /*
     * The realm activation is done last, when the VM starts, after all images
     * have been loaded and all vcpus finalized.
     */
    qemu_add_vm_change_state_handler(rme_vm_state_change, NULL);

    rme_guest->rom_load_notifier.notify = rme_rom_load_notify;
    rom_add_load_notifier(&rme_guest->rom_load_notifier);

    ms->require_guest_memfd = true;
    cgs->ready = true;
    return 0;
}

/*
 * kvm_arm_rme_init_guest_ram - Initialize a Realm IPA range
 */
void kvm_arm_rme_init_guest_ram(hwaddr base, size_t size)
{
    if (rme_guest) {
        rme_add_ram_region(base, size, /* populate */ false);
    }
}

int kvm_arm_rme_vcpu_init(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (rme_guest) {
        cpu->kvm_rme = true;
    }
    return 0;
}

int kvm_arm_rme_vm_type(MachineState *ms)
{
    if (rme_guest) {
        return KVM_VM_TYPE_ARM_REALM;
    }
    return 0;
}

static char *rme_get_rpv(Object *obj, Error **errp)
{
    RmeGuest *guest = RME_GUEST(obj);
    GString *s;
    int i;

    if (!guest->personalization_value) {
        return NULL;
    }

    s = g_string_sized_new(KVM_CAP_ARM_RME_RPV_SIZE * 2 + 1);

    for (i = 0; i < KVM_CAP_ARM_RME_RPV_SIZE; i++) {
        g_string_append_printf(s, "%02x", guest->personalization_value[i]);
    }

    return g_string_free(s, /* free_segment */ false);
}

static void rme_set_rpv(Object *obj, const char *value, Error **errp)
{
    RmeGuest *guest = RME_GUEST(obj);
    size_t len = strlen(value);
    uint8_t *out;
    int i = 1;
    int ret;

    g_free(guest->personalization_value);
    guest->personalization_value = out = g_malloc0(KVM_CAP_ARM_RME_RPV_SIZE);

    /* Two chars per byte */
    if (len > KVM_CAP_ARM_RME_RPV_SIZE * 2) {
        error_setg(errp, "Realm Personalization Value is too large");
        return;
    }

    /* First byte may have a single char */
    if (len % 2) {
        ret = sscanf(value, "%1hhx", out++);
    } else {
        ret = sscanf(value, "%2hhx", out++);
        i++;
    }
    if (ret != 1) {
        error_setg(errp, "Invalid Realm Personalization Value");
        return;
    }

    for (; i < len; i += 2) {
        ret = sscanf(value + i, "%2hhx", out++);
        if (ret != 1) {
            error_setg(errp, "Invalid Realm Personalization Value");
            return;
        }
    }
}

static int rme_get_measurement_algo(Object *obj, Error **errp)
{
    RmeGuest *guest = RME_GUEST(obj);

    return guest->measurement_algo;
}

static void rme_set_measurement_algo(Object *obj, int algo, Error **errp)
{
    RmeGuest *guest = RME_GUEST(obj);

    guest->measurement_algo = algo;
}

static void rme_guest_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "personalization-value", rme_get_rpv,
                                  rme_set_rpv);
    object_class_property_set_description(oc, "personalization-value",
            "Realm personalization value (512-bit hexadecimal number)");

    object_class_property_add_enum(oc, "measurement-algo",
                                   "RmeGuestMeasurementAlgo",
                                   &RmeGuestMeasurementAlgo_lookup,
                                   rme_get_measurement_algo,
                                   rme_set_measurement_algo);
    object_class_property_set_description(oc, "measurement-algo",
            "Realm measurement algorithm ('sha256', 'sha512')");
}

static void rme_guest_instance_init(Object *obj)
{
    if (rme_guest) {
        error_report("a single instance of RmeGuest is supported");
        exit(1);
    }
    rme_guest = RME_GUEST(obj);
    rme_guest->measurement_algo = RME_GUEST_MEASUREMENT_ALGO_SHA512;
}

static const TypeInfo rme_guest_info = {
    .parent = TYPE_CONFIDENTIAL_GUEST_SUPPORT,
    .name = TYPE_RME_GUEST,
    .instance_size = sizeof(struct RmeGuest),
    .instance_init = rme_guest_instance_init,
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
