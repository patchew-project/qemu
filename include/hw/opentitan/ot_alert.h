/*
 * QEMU OpenTitan Alert handler device
 *
 * Copyright (c) 2022-2025 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_OPENTITAN_OT_ALERT
#define HW_OPENTITAN_OT_ALERT

#include "qom/object.h"

#define OPENTITAN_DEVICE_ALERT "ot-alert"

#define TYPE_OT_ALERT "ot-alert"
OBJECT_DECLARE_TYPE(OtAlertState, OtAlertClass, OT_ALERT)

#endif /* HW_OPENTITAN_OT_ALERT */
