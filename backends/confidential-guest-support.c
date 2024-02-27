/*
 * QEMU Confidential Guest support
 *
 * Copyright Red Hat.
 *
 * Authors:
 *  David Gibson <david@gibson.dropbear.id.au>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "exec/confidential-guest-support.h"

OBJECT_DEFINE_ABSTRACT_TYPE(ConfidentialGuestSupport,
                            confidential_guest_support,
                            CONFIDENTIAL_GUEST_SUPPORT,
                            OBJECT)

#if defined(CONFIG_IGVM)
static char *get_igvm(Object *obj, Error **errp)
{
    ConfidentialGuestSupport *cgs = CONFIDENTIAL_GUEST_SUPPORT(obj);
    return g_strdup(cgs->igvm_filename);
}

static void set_igvm(Object *obj, const char *value, Error **errp)
{
    ConfidentialGuestSupport *cgs = CONFIDENTIAL_GUEST_SUPPORT(obj);
    g_free(cgs->igvm_filename);
    cgs->igvm_filename = g_strdup(value);
}
#endif

static void confidential_guest_support_class_init(ObjectClass *oc, void *data)
{
#if defined(CONFIG_IGVM)
    object_class_property_add_str(oc, "igvm-file",
        get_igvm, set_igvm);
    object_class_property_set_description(oc, "igvm-file",
        "Set the IGVM filename to use");
#endif
}

static void confidential_guest_support_init(Object *obj)
{
}

static void confidential_guest_support_finalize(Object *obj)
{
}
