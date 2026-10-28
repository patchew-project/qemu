/*
 * Test for multiple targets
 *
 * Ensures targets compatible can be linked together.
 * Also that each QOM type is unique among them and can be initialized.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Author:
 *  Pierrick Bouvier <pierrick.bouvier@oss.qualcomm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/target-info.h"
#include "qemu/target-info-qom.h"
#include "qom/object.h"

#ifdef CONFIG_SDL
/*
 * SDL insists on wrapping the main() function with its own implementation on
 * some platforms; it does so via a macro that renames our main function, so
 * <SDL.h> must be #included here even with no SDL code called from this file.
 */
#include <SDL.h>
#endif

/* stub for system/main.c */
int (*qemu_main)(void);

static void test_qom_types_are_unique(const void *data)
{
    const TargetInfo *ti = data;

    /* QOM types can be initialized only once, so do it in a subprocess. */
    if (!g_test_subprocess()) {
        /* parent process path - respawn this exact test path */
        g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
        g_test_trap_assert_passed();
        return;
    }
    /* child process path */

    target_info_qom_set_target_info(ti);

    /* register all types */
    module_call_init(MODULE_INIT_QOM);
    /* trigger type_initialize for all types */
    object_class_get_list("", false);
}

static void test_accel_cpu_parent_per_target(const void *data)
{
    const TargetInfo *ti = data;
    g_autofree char *name = NULL;

    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
        g_test_trap_assert_passed();
        return;
    }

    target_info_qom_set_target_info(ti);
    module_call_init(MODULE_INIT_QOM);

    name = g_strconcat("accel-", ti->cpu_type, NULL);
    g_assert_nonnull(object_class_by_name(name));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_TARGET_INFO);
    g_autoptr(GSList) targets = object_class_get_list_sorted(TYPE_TARGET_INFO,
                                                             false);
    g_assert(targets);
    for (GSList *elem = targets; elem; elem = elem->next) {
        const TargetInfo *ti = TARGET_INFO_CLASS(elem->data)->target_info;
        g_autofree char *path =
            g_strdup_printf("/multi-targets/qom-types-are-unique/%s",
                            ti->target_name);
        g_autofree char *accel_path =
            g_strdup_printf("/multi-targets/accel-cpu-parent-per-target/%s",
                            ti->target_name);
        g_test_add_data_func(path, ti, test_qom_types_are_unique);
        g_test_add_data_func(accel_path, ti, test_accel_cpu_parent_per_target);
    }

    return g_test_run();
}
