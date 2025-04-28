/*
 * Virtio RDMA Device
 *
 * Copyright (C) 2025 KylinSoft Inc.
 *
 * Authors:
 *  Weimin Xiong <xiongweimin@kylinos.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _LINUX_VIRTIO_RDMA_H
#define _LINUX_VIRTIO_RDMA_H

#include <linux/types.h>
#include <infiniband/verbs.h>

#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_types.h"

struct virtio_rdma_config {
    __le32         phys_port_cnt;

    __le64         sys_image_guid;
    __le32         vendor_id;
    __le32         vendor_part_id;
    __le32         hw_ver;
    __le64         max_mr_size;
    __le64         page_size_cap;
    __le32         max_qp;
    __le32         max_qp_wr;
    __le64         device_cap_flags;
    __le32         max_send_sge;
    __le32         max_recv_sge;
    __le32         max_sge_rd;
    __le32         max_cq;
    __le32         max_cqe;
    __le32         max_mr;
    __le32         max_pd;
    __le32         max_qp_rd_atom;
    __le32         max_res_rd_atom;
    __le32         max_qp_init_rd_atom;
    __le32         atomic_cap;
    __le32         max_mw;
    __le32         max_mcast_grp;
    __le32         max_mcast_qp_attach;
    __le32         max_total_mcast_qp_attach;
    __le32         max_ah;
    __le32         max_fast_reg_page_list_len;
    __le32         max_pi_fast_reg_page_list_len;
    __le16         max_pkeys;
    uint8_t        local_ca_ack_delay;

    uint8_t           reserved[64];
} QEMU_PACKED;

#endif
