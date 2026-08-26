/*
 * QEMU binary/target API (QOM types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/help_option.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "qemu/target-info-qom.h"
#include "hw/core/boards.h"

const InterfaceInfo type_target_specific[] = {
    { TYPE_TARGET_SPECIFIC },
    { }
};

static const TypeInfo target_info_types[] = {
    {
        .name           = TYPE_TARGET_SPECIFIC,
        .parent         = TYPE_INTERFACE,
        .class_size     = sizeof(TargetSpecificClass),
    },
};

DEFINE_TYPES(target_info_types)

static GSList *filter_types_available(GSList *types)
{
    GSList **link = &types;

    while (*link) {
        ObjectClass *oc = (*link)->data;
        TargetSpecificClass *tsc = (TargetSpecificClass *)
            object_class_dynamic_cast(oc, TYPE_TARGET_SPECIFIC);

        if (tsc && !tsc->is_available) {
            error_setg(&error_fatal,
                       "%s is target specific, but does not "
                       "implement interface", object_class_get_name(oc));
        }
        if (tsc && !tsc->is_available()) {
            *link = g_slist_delete_link(*link, *link);
        } else {
            link = &(*link)->next;
        }
    }

    return types;
}

GSList *get_machine_types_available(void)
{
    GSList *machines = object_class_get_list_sorted(TYPE_MACHINE, false);

    return filter_types_available(machines);
}

static void target_info_qom_class_init(ObjectClass *oc, const void * data)
{
    TargetInfoQomClass *klass = TARGET_INFO_CLASS(oc);
    klass->target_info = data;
}

static const TypeInfo target_info_parent_type = {
    .name = TYPE_TARGET_INFO,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(TargetInfoQom),
    .class_size = sizeof(TargetInfoQomClass),
    /* use class_base_init so children classes can set class_data accordingly */
    .class_base_init = target_info_qom_class_init,
    /* children classes will be concrete, which allows to easily query them
     * without listing this parent class also */
    .abstract = true,
};

DEFINE_TARGET_INFO_TYPE(target_info_parent_type)

static const TargetInfo *target_info_ptr;

const TargetInfo *target_info(void)
{
    return target_info_ptr;
}

static void set_target_info(const TargetInfo *chosen)
{
    target_info_ptr = chosen;
}

static void list_targets_available(void)
{
    printf("List of targets available:\n");
    g_autoptr(GSList) targets = object_class_get_list_sorted(TYPE_TARGET_INFO, false);
    for (GSList *elem = targets; elem; elem = elem->next) {
        const TargetInfo *ti = TARGET_INFO_CLASS(elem->data)->target_info;

        printf("- %s\n", ti->target_name);
    }
}

static bool target_info_matches_name(const TargetInfo *ti, const char *name)
{
    return !strcmp(name, ti->target_name);
}

/* qemu-system-aarch64[.exe] -> aarch64; qemu-system[.exe] -> NULL. */
static const char *target_from_argv0(char *base)
{
    if (g_str_has_prefix(base, "qemu-system-")) {
        return base + strlen("qemu-system-");
    }
    return NULL;
}

void target_info_qom_set_target(const char *name, Error **errp)
{
    g_autoptr(GSList) targets = object_class_get_list(TYPE_TARGET_INFO, false);
    g_autofree char *prg_base = NULL;
    size_t num_found = g_slist_length(targets);

    if (num_found == 0) {
        error_setg(errp, "no target-info is available");
        return;
    }

    if (!name) {
        const char *prg = g_get_prgname();
        if (prg && prg[0]) {
            char *dot;

            prg_base = g_path_get_basename(prg);
            dot = strrchr(prg_base, '.');
            if (dot && g_ascii_strcasecmp(dot, ".exe") == 0) {
                *dot = '\0';
            }
            name = target_from_argv0(prg_base);
        }
    }

    if (name) {
        if (is_help_option(name)) {
            list_targets_available();
            exit(0);
        }
        for (GSList *elem = targets; elem; elem = elem->next) {
            const TargetInfo *ti = TARGET_INFO_CLASS(elem->data)->target_info;
            if (target_info_matches_name(ti, name)) {
                set_target_info(ti);
                return;
            }
        }
        error_setg(errp, "target '%s' is not available, "
                   "use -target ? to list available targets", name);
        return;
    }

    error_setg(errp, "no target specified, "
               "use -target ? to list available targets");
}
