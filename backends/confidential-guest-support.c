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
#include "qemu/error-report.h"

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

static int check_support(ConfidentialGuestPlatformType platform,
                         uint16_t platform_version, uint8_t highest_vtl,
                         uint64_t shared_gpa_boundary)
{
    /* Default: no support. */
    return 0;
}

static int set_guest_state(hwaddr gpa, uint8_t *ptr, uint64_t len,
                                 ConfidentialGuestPageType memory_type,
                                 uint16_t cpu_index)
{
    warn_report("Confidential guest memory not supported");
    return -1;
}

static int get_mem_map_entry(int index, ConfidentialGuestMemoryMapEntry *entry)
{
    return 1;
}

static void confidential_guest_support_init(Object *obj)
{
    ConfidentialGuestSupport *cgs = CONFIDENTIAL_GUEST_SUPPORT(obj);
    cgs->check_support = check_support;
    cgs->set_guest_state = set_guest_state;
    cgs->get_mem_map_entry = get_mem_map_entry;
}

static void confidential_guest_support_finalize(Object *obj)
{
}
