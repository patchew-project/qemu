/*
 * QEMU Arm RME support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright Linaro 2026
 */

#include "qemu/osdep.h"

#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "hw/core/loader.h"
#include "kvm_arm.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "system/confidential-guest-support.h"
#include "system/kvm.h"
#include "system/runstate.h"

#define TYPE_RME_GUEST "rme-guest"
OBJECT_DECLARE_SIMPLE_TYPE(RmeGuest, RME_GUEST)

#define RME_PAGE_SIZE qemu_real_host_page_size()

typedef struct {
    hwaddr base;
    hwaddr size;
    AddressSpace *as;
} RmeRamRegion;

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
    Notifier rom_load_notifier;
    RmeRamRegion init_ram;
    GSList *ram_regions;
};

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(RmeGuest, rme_guest, RME_GUEST,
                                          CONFIDENTIAL_GUEST_SUPPORT,
                                          { TYPE_USER_CREATABLE }, { })

static RmeGuest *rme_guest;

static int rme_populate_range(const RmeRamRegion *region, bool measure,
                              Error **errp)
{
    int ret;
    void *host_ua;
    hwaddr size = region->size;
    hwaddr base = region->base;
    hwaddr start = QEMU_ALIGN_DOWN(base, RME_PAGE_SIZE);
    hwaddr end = QEMU_ALIGN_UP(base + size, RME_PAGE_SIZE);
    struct kvm_arm_rmi_populate populate_args;

    host_ua = address_space_map(region->as, base, &size, false,
                                MEMTXATTRS_UNSPECIFIED);

    populate_args = (struct kvm_arm_rmi_populate) {
        .base = start,
        .size = end - start,
        .source_uaddr = (uintptr_t)host_ua,
        .flags = measure ? KVM_ARM_RMI_POPULATE_FLAGS_MEASURE : 0,
    };

    while (populate_args.size > 0) {
        ret = kvm_vm_ioctl(kvm_state, KVM_ARM_RMI_POPULATE, &populate_args, 0);
        if (ret) {
            error_setg_errno(errp, -ret,
                "failed to populate realm [0x%"HWADDR_PRIx", 0x%"HWADDR_PRIx")",
                start, end);
            break;
        }
    }

    address_space_unmap(region->as, host_ua, size, false, 0);

    return ret;
}

static void rme_populate_ram_region(gpointer data, gpointer err)
{
    Error **errp = err;
    const RmeRamRegion *region = data;

    if (*errp) {
        return;
    }

    rme_populate_range(region, /* measure */ true, errp);
}

static void rme_vm_state_change(void *opaque, bool running, RunState state)
{
    Error *errp = NULL;

    if (!running) {
        return;
    }

    g_slist_foreach(rme_guest->ram_regions, rme_populate_ram_region, &errp);
    g_slist_free_full(g_steal_pointer(&rme_guest->ram_regions), g_free);
    if (errp) {
        return;
    }

    kvm_mark_guest_state_protected();
}

static void rme_guest_class_init(ObjectClass *oc, const void *data)
{
}

static void rme_guest_init(Object *obj)
{
    if (rme_guest) {
        error_report("a single instance of RmeGuest is supported");
        exit(1);
    }
    rme_guest = RME_GUEST(obj);
}

static void rme_guest_finalize(Object *obj)
{
}

static gint rme_compare_ram_regions(gconstpointer a, gconstpointer b)
{
    const RmeRamRegion *ra = a;
    const RmeRamRegion *rb = b;

    g_assert(ra->base != rb->base);
    return ra->base < rb->base ? -1 : 1;
}

static void rme_rom_load_notify(Notifier *notifier, void *data)
{
    RmeRamRegion *region;
    RomLoaderNotifyData *rom = data;

    if (rom->addr == -1) {
        /*
         * These blobs (ACPI tables) are not loaded into guest RAM at reset.
         * Instead the firmware will load them via fw_cfg and measure them
         * itself.
         */
        return;
    }

    region = g_new0(RmeRamRegion, 1);
    region->base = rom->addr;
    region->size = rom->len;
    region->as = rom->as;

    /*
     * The Realm Initial Measurement (RIM) depends on the order in which we
     * initialize and populate the RAM regions. To help a verifier
     * independently calculate the RIM, sort regions by GPA.
     */
    rme_guest->ram_regions = g_slist_insert_sorted(rme_guest->ram_regions,
                                                   region,
                                                   rme_compare_ram_regions);
}

int kvm_arm_rme_init(MachineState *ms, KVMState *s)
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

    if (!kvm_vm_check_extension(s, KVM_CAP_ARM_RMI)) {
        error_report("KVM doesn't support Realms");
        return -ENODEV;
    }

    error_setg(&rme_mig_blocker, "RME: migration is not implemented");
    migrate_add_blocker(&rme_mig_blocker, &error_fatal);

    rme_guest->rom_load_notifier.notify = rme_rom_load_notify;
    rom_add_load_notifier(&rme_guest->rom_load_notifier);

    /*
     * The realm activation is done last, when the VM starts, after all images
     * have been loaded and all vcpus finalized.
     */
    qemu_add_vm_change_state_handler(rme_vm_state_change, NULL);

    cgs->require_guest_memfd = true;
    cgs->ready = true;
    return 0;
}

void kvm_arm_rme_init_guest_ram(hwaddr base, size_t size)
{
    if (!rme_guest) {
        return;
    }

    rme_guest->init_ram.base = base;
    rme_guest->init_ram.size = size;
    rme_guest->init_ram.as = NULL;
}

void kvm_arm_rme_vcpu_init(ARMCPU *cpu)
{
    if (!rme_guest) {
        return;
    }

    cpu->kvm_rme = true;
}

int kvm_arm_rme_vm_type(void)
{
    if (rme_guest) {
        return KVM_VM_TYPE_ARM_REALM;
    }
    return 0;
}
