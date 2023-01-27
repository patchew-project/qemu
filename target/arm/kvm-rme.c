/*
 * QEMU Arm RME support
 *
 * Copyright Linaro 2022
 */

#include "qemu/osdep.h"

#include "exec/confidential-guest-support.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "kvm_arm.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"

#define TYPE_RME_GUEST "rme-guest"
OBJECT_DECLARE_SIMPLE_TYPE(RmeGuest, RME_GUEST)

typedef struct RmeGuest RmeGuest;

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
};

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

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_RME, 0,
                            KVM_CAP_ARM_RME_ACTIVATE_REALM);
    if (ret) {
        error_setg_errno(&error_fatal, -ret, "RME: failed to activate realm");
    }
}

int kvm_arm_rme_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    int ret;
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

static void rme_guest_class_init(ObjectClass *oc, void *data)
{
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
