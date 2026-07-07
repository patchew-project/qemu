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
} RmeRamRegion;

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
    RmeRamRegion init_ram;
};

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(RmeGuest, rme_guest, RME_GUEST,
                                          CONFIDENTIAL_GUEST_SUPPORT,
                                          { TYPE_USER_CREATABLE }, { })

static RmeGuest *rme_guest;

static void rme_vm_state_change(void *opaque, bool running, RunState state)
{
    if (!running) {
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
