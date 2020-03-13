/*
 * QAPI x86 CPU features stub
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-machine-target.h"

void visit_type_X86CPUFeatureWordInfoList(Visitor *v, const char *name,
                                      X86CPUFeatureWordInfoList **obj,
                                      Error **errp)
{
}
