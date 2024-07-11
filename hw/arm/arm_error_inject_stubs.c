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
#include "qapi-commands-arm-error-inject.h"

void qmp_arm_inject_error(ArmProcessorErrorTypeList *errortypes, Error **errp)
{
    error_setg(errp, "ARM processor error support is not compiled in");
}
