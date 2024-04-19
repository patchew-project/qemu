/*
 * QEMU Arm RME support
 *
 * Copyright Linaro 2024
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

struct RmeGuest {
    ConfidentialGuestSupport parent_obj;
};

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
