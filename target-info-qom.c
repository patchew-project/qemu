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

static const TargetInfo *target_info_from_class(ObjectClass *oc)
{
    return TARGET_INFO_CLASS(oc)->target_info;
}

/* Default token is target_name; the other endian is target_name-le or -be. */
static char *target_info_option_name(const TargetInfo *ti)
{
    if (ti->is_default) {
        return g_strdup(ti->target_name);
    }
    if (ti->endianness == ENDIAN_MODE_BIG) {
        return g_strdup_printf("%s-be", ti->target_name);
    }
    return g_strdup_printf("%s-le", ti->target_name);
}

static gint target_info_token_name_cmp(gconstpointer a, gconstpointer b,
                                       gpointer data)
{
    const TargetInfo *ta = target_info_from_class((ObjectClass *)a);
    const TargetInfo *tb = target_info_from_class((ObjectClass *)b);
    g_autofree char *na = target_info_option_name(ta);
    g_autofree char *nb = target_info_option_name(tb);

    return strcmp(na, nb);
}

static void G_NORETURN target_info_list_and_exit(GSList *targets)
{
    printf("Supported targets:\n");
    for (GSList *l = targets; l; l = l->next) {
        const TargetInfo *ti = target_info_from_class(l->data);
        g_autofree char *opt_name = target_info_option_name(ti);

        printf("  %s%s\n", opt_name, ti->is_default ? " (default)" : "");
    }
    exit(0);
}

static const TargetInfo *target_info_find_default(GSList *targets,
                                                  size_t *num_default)
{
    const TargetInfo *chosen = NULL;

    *num_default = 0;
    for (GSList *l = targets; l; l = l->next) {
        const TargetInfo *ti = target_info_from_class(l->data);

        if (ti->is_default) {
            (*num_default)++;
            chosen = ti;
        }
    }
    return chosen;
}

static const TargetInfo *target_info_find_token(GSList *targets,
                                                const char *token)
{
    for (GSList *l = targets; l; l = l->next) {
        const TargetInfo *ti = target_info_from_class(l->data);
        g_autofree char *opt_name = target_info_option_name(ti);

        if (!strcmp(opt_name, token)) {
            return ti;
        }
    }
    return NULL;
}

static bool target_info_base_match_token(const char *base, const char *token)
{
    size_t nlen = strlen(token);
    size_t blen;

    if (!base || !token[0] || !g_str_has_suffix(base, token)) {
        return false;
    }
    blen = strlen(base);
    return blen == nlen || base[blen - nlen - 1] == '-';
}

static const TargetInfo *target_info_find_prgname(GSList *targets,
                                                  const char *base)
{
    const TargetInfo *chosen = NULL;
    size_t best_len = 0;

    if (!base || !base[0]) {
        return NULL;
    }

    for (GSList *l = targets; l; l = l->next) {
        const TargetInfo *ti = target_info_from_class(l->data);
        g_autofree char *opt_name = target_info_option_name(ti);
        size_t nlen = strlen(opt_name);

        if (nlen > best_len &&
            target_info_base_match_token(base, opt_name)) {
            chosen = ti;
            best_len = nlen;
        }
    }
    return chosen;
}

static const TargetInfo *target_info_from_prgname(GSList *targets)
{
    const char *prg = g_get_prgname();
    g_autofree char *base = NULL;
    char *dot;
    const TargetInfo *chosen;

    if (!prg || !prg[0]) {
        return NULL;
    }

    base = g_path_get_basename(prg);
    dot = strrchr(base, '.');
    if (dot && g_ascii_strcasecmp(dot, ".exe") == 0) {
        *dot = '\0';
    }

    chosen = target_info_find_prgname(targets, base);
    if (!chosen && strlen(base) > 1 && g_str_has_suffix(base, "w")) {
        base[strlen(base) - 1] = '\0';
        chosen = target_info_find_prgname(targets, base);
    }
    return chosen;
}

void target_info_qom_set_target(const char *name)
{
    g_autoptr(GSList) targets = object_class_get_list(TYPE_TARGET_INFO, false);
    const TargetInfo *chosen = NULL;
    size_t num_found = g_slist_length(targets);
    size_t num_default = 0;

    if (num_found == 0) {
        error_setg(&error_fatal, "no target-info is available");
    }

    if (name && is_help_option(name)) {
        targets = g_slist_sort_with_data(targets, target_info_token_name_cmp,
                                         NULL);
        target_info_list_and_exit(targets);
    }

    if (name && name[0]) {
        chosen = target_info_find_token(targets, name);
        if (!chosen) {
            error_setg(&error_fatal, "unknown target '%s'", name);
        }
        target_info_ptr = chosen;
        return;
    }

    chosen = target_info_from_prgname(targets);
    if (chosen) {
        target_info_ptr = chosen;
        return;
    }

    if (num_found == 1) {
        target_info_ptr = target_info_from_class(targets->data);
        return;
    }

    chosen = target_info_find_default(targets, &num_default);
    if (num_default != 1) {
        error_setg(&error_fatal, num_default == 0 ?
                                 "no default target-info is available" :
                                 "multiple default targets; use -target");
    }

    target_info_ptr = chosen;
}
