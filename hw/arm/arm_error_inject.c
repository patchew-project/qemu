/*
 * ARM Processor error injection
 *
 * Copyright(C) 2024 Huawei LTD.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi-commands-arm-error-inject.h"
#include "hw/boards.h"
#include "hw/acpi/ghes.h"

/* For ARM processor errors */
void qmp_arm_inject_error(ArmProcessorErrorTypeList *errortypes, Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    uint8_t error_types = 0;

    while (errortypes) {
        error_types |= BIT(errortypes->value);
        errortypes = errortypes->next;
    }

    ghes_record_arm_errors(error_types, ACPI_GHES_NOTIFY_GPIO);
    if (mc->set_error) {
        mc->set_error();
    }

    return;
}
