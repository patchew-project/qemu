/*
 * QAPI helpers for target specific QMP commands
 *
 * SPDX-FileCopyrightText: 2024 Linaro Ltd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/commands-target-compat.h"
#include "sysemu/arch_init.h"
#include "hw/core/cpu.h"
#include "hw/core/sysemu-cpu-ops.h"

static void cpu_common_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = cpu_model_from_type(typename);
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

static void arch_add_cpu_definitions(CpuDefinitionInfoList **cpu_list,
                                     const char *cpu_typename)
{
    ObjectClass *oc;
    GSList *list;
    const struct SysemuCPUOps *ops;

    oc = object_class_by_name(cpu_typename);
    if (!oc) {
        return;
    }
    ops = CPU_CLASS(oc)->sysemu_ops;

    list = object_class_get_list(cpu_typename, false);
    if (ops->cpu_list_compare) {
        list = g_slist_sort(list, ops->cpu_list_compare);
    }
    g_slist_foreach(list, ops->add_definition ? : cpu_common_add_definition,
                    cpu_list);
    g_slist_free(list);

    if (ops->add_alias_definitions) {
        ops->add_alias_definitions(cpu_list);
    }
}

CpuDefinitionInfoList *generic_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;

    for (unsigned i = 0; i <= QEMU_ARCH_BIT_LAST; i++) {
        const char *cpu_typename;

        cpu_typename = cpu_typename_by_arch_bit(i);
        if (!cpu_typename) {
            continue;
        }
        arch_add_cpu_definitions(&cpu_list, cpu_typename);
    }

    return cpu_list;
}
