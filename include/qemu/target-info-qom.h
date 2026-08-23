/*
 * QEMU target info QOM types
 *
 * Copyright (c) Qualcomm
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_QOM_H
#define QEMU_TARGET_INFO_QOM_H

#include "qemu/target-info-impl.h"
#include "qapi/error.h"
#include "qemu/compiler.h"
#include "qom/object.h"

#include <stddef.h>

#define TYPE_TARGET_INFO "target-info"

#define TYPE_TARGET_SPECIFIC "target-specific"

typedef struct TargetSpecific TargetSpecific;

typedef struct TargetSpecificClass {
    InterfaceClass parent_class;

    bool (*is_available)(void);
} TargetSpecificClass;

#define TARGET_SPECIFIC(obj) \
    INTERFACE_CHECK(TargetSpecific, (obj), TYPE_TARGET_SPECIFIC)
DECLARE_CLASS_CHECKERS(TargetSpecificClass, TARGET_SPECIFIC,
                       TYPE_TARGET_SPECIFIC)

typedef struct TargetInfoQom {
    Object parent_obj;
} TargetInfoQom;

typedef struct TargetInfoQomClass {
    ObjectClass parent_class;
    const TargetInfo *target_info;
} TargetInfoQomClass;

OBJECT_DECLARE_TYPE(TargetInfoQom, TargetInfoQomClass, TARGET_INFO)

typedef struct ArchDumpInfo ArchDumpInfo;
struct GuestPhysBlockList;
typedef struct CPUSemihostingOps CPUSemihostingOps;

typedef struct TargetCpuOps {
    CpuDefinitionInfoList *(*query_cpu_definitions)(Error **errp);
    CpuModelExpansionInfo *(*query_cpu_model_expansion)(
        CpuModelExpansionType type, CpuModelInfo *model, Error **errp);
    int (*get_dump_info)(ArchDumpInfo *info,
                         const struct GuestPhysBlockList *guest_phys_blocks);
    ssize_t (*get_note_size)(int class, int machine, int nr_cpus);
    const CPUSemihostingOps *semihosting;
} TargetCpuOps;

/**
 * target_info_register_cpu_op:
 * @cpu_type: CPU_RESOLVING_TYPE of the registering architecture
 * @offset: offsetof(TargetCpuOps, member) for the slot being filled
 * @impl: handler or ops table stored at that offset
 *
 * Combined binaries merge one member at a time so QMP, dump, and
 * semihosting (or split QMP files) can register independently.
 * MODULE_INIT_QOM runs after target_info_qom_set_target(), so only
 * the selected cpu_type is stored.
 */
void target_info_register_cpu_op(const char *cpu_type, size_t offset,
                                 void *impl);
const TargetCpuOps *target_info_cpu_ops(void);

#define TARGET_INFO_CPU_OP(cpu_type, member, impl)                            \
static void glue(target_info_cpu_op_, impl)(void)                             \
{                                                                             \
    target_info_register_cpu_op((cpu_type), offsetof(TargetCpuOps, member),   \
                                (void *)&(impl));                             \
}                                                                             \
type_init(glue(target_info_cpu_op_, impl))

/**
 * target_info_qom_set_target:
 * @name: -target token, or NULL/%empty to infer from the program
 * basename or the unique default
 *
 * Tokens are target_name (arch default endian) or target_name-be /
 * target_name-le. -target overrides the program basename.
 */
void target_info_qom_set_target(const char *name);

/**
 * get_machine_types_available:
 *
 * Returns: Machine classes available for the selected target.
 */
GSList *get_machine_types_available(void);

extern const InterfaceInfo type_target_specific[];

#endif /* QEMU_TARGET_INFO_QOM_H */
