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

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
    Notifier rom_load_notifier;
    GSList *ram_regions;
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

    cgs->ready = true;
    return 0;
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

static void rme_guest_class_init(ObjectClass *oc, void *data)
{
}

static void rme_guest_instance_init(Object *obj)
{
    if (rme_guest) {
        error_report("a single instance of RmeGuest is supported");
        exit(1);
    }
    rme_guest = RME_GUEST(obj);
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
