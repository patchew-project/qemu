/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MCDSTUB_H
#define MCDSTUB_H

#include "qemu/osdep.h"
#include "qapi/error.h"

bool mcd_monitor_start(const char *device, Error **errp);

#endif /* MCDSTUB_H */
