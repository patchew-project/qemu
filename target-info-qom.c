/*
 * QEMU binary/target API (QOM types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qemu/error-report.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "qemu/target-info-qom.h"

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

void target_info_qom_set_target(void)
{
    g_autoptr(GSList) targets = object_class_get_list(TYPE_TARGET_INFO, false);

    size_t num_found = g_slist_length(targets);
    if (num_found != 1) {
        error_setg(&error_fatal, num_found == 0 ?
                                 "no target-info is available" :
                                 "more than one target-info is available");
    }
    target_info_ptr = TARGET_INFO_CLASS(targets->data)->target_info;
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

void target_info_qom_set_target_from_name(const char *name)
{
    if (!strcmp(name, "help")) {
        list_targets_available();
        exit(0);
    }

    g_autoptr(GSList) targets = object_class_get_list(TYPE_TARGET_INFO, false);
    for (GSList *elem = targets; elem; elem = elem->next) {
        const TargetInfo *ti = TARGET_INFO_CLASS(elem->data)->target_info;
        if (!strcmp(name, ti->target_name)) {
            target_info_ptr = ti;
            return;
        }
    }

    error_report("target '%s' is not available", name);
    list_targets_available();
    exit(1);
}

void target_info_qom_set_target_from_argv0(const char *argv0)
{
    g_autoptr(GSList) targets = object_class_get_list(TYPE_TARGET_INFO, false);
    size_t num_targets = g_slist_length(targets);
    g_assert(num_targets > 0);

    /* if we have only one target, set it as default */
    if (num_targets == 1) {
        const TargetInfo *ti = TARGET_INFO_CLASS(targets->data)->target_info;
        target_info_ptr = ti;
        return;
    }

    g_autofree char *basename_cstr = g_path_get_basename(argv0);
    g_autoptr(GString) basename = g_string_new(basename_cstr);

    const char *rm_common_patterns[] = {"qemu-system",
                                        "qemu-fuzz",
                                        "-target",
                                        "-unsigned",
                                        ".exe",
                                        NULL};
    for (const char **rm = rm_common_patterns; *rm; ++rm) {
        size_t len = strlen(*rm);
        char *found = strstr(basename->str, *rm);
        if (found) {
            size_t pos = found - basename->str;
            g_string_erase(basename, pos, len);
        }
    }

    /* remove ^[-.] */
    const char *rm_separator[] = {"-", ".", NULL};
    for (const char **rm = rm_separator; *rm; ++rm) {
        size_t len = strlen(*rm);
        if (!strncmp(basename->str, *rm, len)) {
            g_string_erase(basename, 0, len);
        }
    }

    /* remove [-.]{ok, ko, this-is-my-custom-binary-name, .*} */
    for (const char **rm = rm_separator; *rm; ++rm) {
        char *end = strstr(basename->str, *rm);
        if (end) {
            size_t pos = end - basename->str;
            size_t len = basename->len - pos;
            g_string_erase(basename, pos, len);
        }
    }

    if (basename->len > 0) {
        target_info_qom_set_target_from_name(basename->str);
        return;
    }

    /* we have a qemu-system single-binary and multiple targets */
    error_report("missing -target parameter");
    list_targets_available();
    exit(1);
}
