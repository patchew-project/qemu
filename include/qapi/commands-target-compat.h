/*
 * QAPI helpers for target specific QMP commands
 *
 * SPDX-FileCopyrightText: 2024 Linaro Ltd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QAPI_COMPAT_TARGET_H
#define QAPI_COMPAT_TARGET_H

#include "qapi/qapi-types-machine.h"

CpuDefinitionInfoList *generic_query_cpu_definitions(Error **errp);

#endif
