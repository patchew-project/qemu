/*
 * QEMU QOM qdev deprecation helpers
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_QDEV_DEPRECATED_H
#define HW_QDEV_DEPRECATED_H

/**
 * qdev_warn_deprecated_function_used:
 *
 * Display a warning that deprecated code is used.
 */
#define qdev_warn_deprecated_function_used() \
    qdev_warn_deprecated_function(__func__)
void qdev_warn_deprecated_function(const char *function);

#endif
