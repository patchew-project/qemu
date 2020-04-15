/*
 * QEMU VM Introspection
 *
 * Copyright (C) 2018-2020 Bitdefender S.R.L.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_VMI_INTERCEPT_H
#define QEMU_VMI_INTERCEPT_H

typedef enum {
    VMI_INTERCEPT_NONE = 0,
    VMI_INTERCEPT_SUSPEND,
    VMI_INTERCEPT_RESUME,
} VMI_intercept_command;

bool vm_introspection_intercept(VMI_intercept_command ic, Error **errp);

#endif /* QEMU_VMI_INTERCEPT_H */
