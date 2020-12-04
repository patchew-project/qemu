/*
 * PEF (Protected Execution Facility) for POWER support
 *
 * Copyright David Gibson, Redhat Inc. 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "sysemu/kvm.h"
#include "migration/blocker.h"
#include "exec/securable-guest-memory.h"
#include "hw/ppc/pef.h"

#define TYPE_PEF_GUEST "pef-guest"
#define PEF_GUEST(obj)                                  \
    OBJECT_CHECK(PefGuestState, (obj), TYPE_PEF_GUEST)

typedef struct PefGuestState PefGuestState;

/**
 * PefGuestState:
 *
 * The PefGuestState object is used for creating and managing a PEF
 * guest.
 *
 * # $QEMU \
 *         -object pef-guest,id=pef0 \
 *         -machine ...,securable-guest-memory=pef0
 */
struct PefGuestState {
    Object parent_obj;
};

#ifdef CONFIG_KVM
static Error *pef_mig_blocker;

static int kvmppc_svm_init(Error **errp)

int kvmppc_svm_init(SecurableGuestMemory *sgm, Error **errp)
{
    if (!kvm_check_extension(kvm_state, KVM_CAP_PPC_SECURABLE_GUEST)) {
        error_setg(errp,
                   "KVM implementation does not support Secure VMs (is an ultravisor running?)");
        return -1;
    } else {
        int ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_PPC_SECURE_GUEST, 0, 1);

        if (ret < 0) {
            error_setg(errp,
                       "Error enabling PEF with KVM");
            return -1;
        }
    }

    /* add migration blocker */
    error_setg(&pef_mig_blocker, "PEF: Migration is not implemented");
    /* NB: This can fail if --only-migratable is used */
    migrate_add_blocker(pef_mig_blocker, &error_fatal);

    return 0;
}

/*
 * Don't set error if KVM_PPC_SVM_OFF ioctl is invoked on kernels
 * that don't support this ioctl.
 */
void kvmppc_svm_off(Error **errp)
{
    int rc;

    if (!kvm_enabled()) {
        return;
    }

    rc = kvm_vm_ioctl(KVM_STATE(current_accel()), KVM_PPC_SVM_OFF);
    if (rc && rc != -ENOTTY) {
        error_setg_errno(errp, -rc, "KVM_PPC_SVM_OFF ioctl failed");
    }
}
#else
static int kvmppc_svm_init(Error **errp)
{
    g_assert_not_reached();
}
#endif

int pef_kvm_init(SecurableGuestMemory *sgm, Error **errp)
{
    if (!object_dynamic_cast(OBJECT(sgm), TYPE_PEF_GUEST)) {
        return 0;
    }

    if (!kvm_enabled()) {
        error_setg(errp, "PEF requires KVM");
        return -1;
    }

    return kvmppc_svm_init(errp);
}

static const TypeInfo pef_guest_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_PEF_GUEST,
    .instance_size = sizeof(PefGuestState),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_SECURABLE_GUEST_MEMORY },
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
pef_register_types(void)
{
    type_register_static(&pef_guest_info);
}

type_init(pef_register_types);
