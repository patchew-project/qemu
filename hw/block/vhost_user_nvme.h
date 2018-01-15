#ifndef HW_VHOST_USER_NVME_H
#define HW_VHOST_USER_NVME_H
/*
 * vhost-user-nvme
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 *
 *  Author:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "hw/pci/pci.h"
#include "hw/block/block.h"
#include "nvme.h"

int vhost_dev_nvme_set_guest_notifier(struct vhost_dev *hdev,
                                      EventNotifier *notifier, uint32_t qid);
int vhost_dev_nvme_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout);
void vhost_dev_nvme_cleanup(struct vhost_dev *hdev);


int
vhost_user_nvme_io_cmd_pass(struct vhost_dev *dev, uint16_t qid,
                            uint16_t tail_head, bool submission_queue);
int vhost_user_nvme_admin_cmd_raw(struct vhost_dev *dev, NvmeCmd *cmd,
                                  void *buf, uint32_t len);
int vhost_user_nvme_get_cap(struct vhost_dev *dev, uint64_t *cap);
int vhost_dev_nvme_set_backend_type(struct vhost_dev *dev,
                                    VhostBackendType backend_type);
int vhost_dev_nvme_start(struct vhost_dev *hdev, VirtIODevice *vdev);
int vhost_dev_nvme_stop(struct vhost_dev *hdev);

#endif
