/*
 * QMP stub for ARM processor error injection.
 *
 * Copyright(C) 2024 Huawei LTD.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/acpi/ghes.h"

void qmp_arm_inject_error(bool has_validation,
                        ArmProcessorValidationBitsList *validation,
                        bool has_affinity_level,
                        uint8_t affinity_level,
                        bool has_mpidr_el1,
                        uint64_t mpidr_el1,
                        bool has_midr_el1,
                        uint64_t midr_el1,
                        bool has_running_state,
                        ArmProcessorRunningStateList *running_state,
                        bool has_psci_state,
                        uint32_t psci_state,
                        bool has_context, ArmProcessorContextList *context,
                        bool has_vendor_specific, uint8List *vendor_specific,
                        bool has_error,
                        ArmPeiList *error,
                        Error **errp)
{
    error_setg(errp, "ARM processor error support is not compiled in");
}
