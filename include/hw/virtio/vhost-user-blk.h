/*
 * vhost-user-blk host device
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * Based on vhost-scsi.h, Copyright IBM, Corp. 2011
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_USER_BLK_H
#define VHOST_USER_BLK_H

#include "chardev/char-fe.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-blk-common.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_BLK "vhost-user-blk"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserBlk, VHOST_USER_BLK)

struct VHostUserBlk {
    VHostBlkCommon parent_obj;
    CharBackend chardev;
    VhostUserState vhost_user;
    /*
     * There are at least two steps of initialization of the
     * vhost-user device. The first is a "connect" step and
     * second is a "start" step. Make a separation between
     * those initialization phases by using two fields.
     */
    /* vhost_user_blk_connect/vhost_user_blk_disconnect */
    bool connected;
};

#endif
