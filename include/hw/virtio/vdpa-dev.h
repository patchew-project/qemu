/*
 * Vhost Vdpa Device
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All Rights Reserved.
 *
 * Authors:
 *   Longpeng <longpeng2@huawei.com>
 *
 * Largely based on the "vhost-user-blk.h" implemented by:
 *   Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef _VHOST_VDPA_DEVICE_H
#define _VHOST_VDPA_DEVICE_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-vdpa.h"
#include "qom/object.h"


#define TYPE_VHOST_VDPA_DEVICE "vhost-vdpa-device"
OBJECT_DECLARE_SIMPLE_TYPE(VhostVdpaDevice, VHOST_VDPA_DEVICE)

struct VhostVdpaDevice {
    VirtIODevice parent_obj;
};

#endif
