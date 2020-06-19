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
#include "exec/host-trust-limitation.h"

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
 *         -machine ...,host-trust-limitation=pef0
 */
struct PefGuestState {
    Object parent_obj;
};

static Error *pef_mig_blocker;

static int pef_kvm_init(HostTrustLimitation *gmpo, Error **errp)
{
    if (!kvm_check_extension(kvm_state, KVM_CAP_PPC_SECURE_GUEST)) {
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
    migrate_add_blocker(pef_mig_blocker, &error_abort);

    return 0;
}

static void pef_guest_class_init(ObjectClass *oc, void *data)
{
    HostTrustLimitationClass *gmpc = HOST_TRUST_LIMITATION_CLASS(oc);

    gmpc->kvm_init = pef_kvm_init;
}

static const TypeInfo pef_guest_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_PEF_GUEST,
    .instance_size = sizeof(PefGuestState),
    .class_init = pef_guest_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOST_TRUST_LIMITATION },
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
