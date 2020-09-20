/*
 * A device for memory hot-add protocols
 *
 * Copyright (C) 2020 Oracle and/or its affiliates.
 *
 * Author: Maciej S. Szmigiero <maciej.szmigiero@oracle.com>
 *
 * Heavily based on pc-dimm.h:
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *  Vasilis Liaskovitis <vasilis.liaskovitis@profitbricks.com>
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_HAPROT_H
#define QEMU_HAPROT_H

#include "exec/memory.h"
#include "hw/qdev-core.h"

#define TYPE_HAPROT "mem-haprot"
#define HAPROT(obj) \
    OBJECT_CHECK(HAProtDevice, (obj), TYPE_HAPROT)
#define HAPROT_CLASS(oc) \
    OBJECT_CLASS_CHECK(HAProtDeviceClass, (oc), TYPE_HAPROT)
#define HAPROT_GET_CLASS(obj) \
    OBJECT_GET_CLASS(HAProtDeviceClass, (obj), TYPE_HAPROT)

#define HAPROT_ADDR_PROP "addr"
#define HAPROT_NODE_PROP "node"
#define HAPROT_SIZE_PROP "size"
#define HAPROT_MEMDEV_PROP "memdev"

typedef struct HAProtDevice {
    /* private */
    DeviceState parent_obj;

    /* public */
    uint64_t addr;
    uint32_t node;
    HostMemoryBackend *hostmem;
    bool busy;
} HAProtDevice;

typedef uint64_t (*HAProtocolGetAlign)(void *ctx, HAProtDevice *haprot);
typedef void (*HAProtocolPlugNotify)(void *ctx, HAProtDevice *haprot,
                                     Error **errp);
typedef void (*HAProtocolUnplugNotify)(void *ctx, HAProtDevice *haprot);

typedef struct HAProtDeviceClass {
    /* private */
    DeviceClass parent_class;
    HAProtocolGetAlign get_align_cb;
    HAProtocolPlugNotify plug_notify_cb;
    HAProtocolUnplugNotify unplug_notify_cb;
    void *notify_cb_ctx;
} HAProtDeviceClass;

void haprot_register_protocol(HAProtocolGetAlign get_align_cb,
                              HAProtocolPlugNotify plug_notify_cb,
                              HAProtocolUnplugNotify unplug_notify_cb,
                              void *notify_ctx, Error **errp);
void haprot_unregister_protocol(HAProtocolPlugNotify plug_notify_cb,
                                Error **errp);

#endif
