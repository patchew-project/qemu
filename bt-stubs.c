/*
 * Bluetooth stubs.
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/bt.h"

int bt_parse(const char *opt)
{
    error_report("Bluetooth support is disabled");

    return 1;
}
