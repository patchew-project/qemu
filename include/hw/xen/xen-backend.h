/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BACKEND_H
#define HW_XEN_BACKEND_H

#include "hw/xen/xen-bus.h"

typedef void (*XenBackendDeviceCreate)(BusState *bus, const char *name,
                                       QDict *opts, Error **errp);

typedef struct XenBackendInfo {
    const char *type;
    XenBackendDeviceCreate create;
} XenBackendInfo;

void xen_backend_register(const XenBackendInfo *info);

void xen_backend_device_create(BusState *bus, const char *type,
                               const char *name, QDict *opts, Error **errp);

#endif /* HW_XEN_BACKEND_H */
