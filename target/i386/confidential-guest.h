/*
 * x86-specific confidential guest methods.
 *
 * Copyright (c) 2024 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TARGET_I386_CG_H
#define TARGET_I386_CG_H

#include "qom/object.h"

#include "exec/confidential-guest-support.h"

#define TYPE_X86_CONFIDENTIAL_GUEST "x86-confidential-guest"

OBJECT_DECLARE_TYPE(X86ConfidentialGuest,
                    X86ConfidentialGuestClass,
                    X86_CONFIDENTIAL_GUEST)

struct X86ConfidentialGuest {
    /* <private> */
    ConfidentialGuestSupport parent_obj;
};

/**
 * X86ConfidentialGuestClass:
 *
 * Class to be implemented by confidential-guest-support concrete objects
 * for the x86 target.
 */
struct X86ConfidentialGuestClass {
    /* <private> */
    ConfidentialGuestSupportClass parent;

    /* <public> */
    int (*kvm_type)(X86ConfidentialGuest *cg);
};

/**
 * x86_confidential_guest_kvm_type:
 *
 * Calls #X86ConfidentialGuestClass.kvm_type() callback.
 */
static inline int x86_confidential_guest_kvm_type(X86ConfidentialGuest *cg)
{
    X86ConfidentialGuestClass *klass = X86_CONFIDENTIAL_GUEST_GET_CLASS(cg);

    if (klass->kvm_type) {
        return klass->kvm_type(cg);
    } else {
        return 0;
    }
}
#endif
