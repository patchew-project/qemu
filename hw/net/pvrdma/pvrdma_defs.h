/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PVRDMA_DEFS_H
#define PVRDMA_DEFS_H

#include <hw/net/pvrdma/pvrdma_types.h>
#include <hw/net/pvrdma/pvrdma_ib_verbs.h>
#include <hw/net/pvrdma/pvrdma-uapi.h>

/*
 * Masks and accessors for page directory, which is a two-level lookup:
 * page directory -> page table -> page. Only one directory for now, but we
 * could expand that easily. 9 bits for tables, 9 bits for pages, gives one
 * gigabyte for memory regions and so forth.
 */

#define PVRDMA_PDIR_SHIFT        18
#define PVRDMA_PTABLE_SHIFT        9
#define PVRDMA_PAGE_DIR_DIR(x)        (((x) >> PVRDMA_PDIR_SHIFT) & 0x1)
#define PVRDMA_PAGE_DIR_TABLE(x)    (((x) >> PVRDMA_PTABLE_SHIFT) & 0x1ff)
#define PVRDMA_PAGE_DIR_PAGE(x)        ((x) & 0x1ff)
#define PVRDMA_PAGE_DIR_MAX_PAGES    (1 * 512 * 512)
#define PVRDMA_MAX_FAST_REG_PAGES    128

/*
 * Max MSI-X vectors.
 */

#define PVRDMA_MAX_INTERRUPTS    3

/* Register offsets within PCI resource on BAR1. */
#define PVRDMA_REG_VERSION    0x00    /* R: Version of device. */
#define PVRDMA_REG_DSRLOW    0x04    /* W: Device shared region low PA. */
#define PVRDMA_REG_DSRHIGH    0x08    /* W: Device shared region high PA. */
#define PVRDMA_REG_CTL        0x0c    /* W: PVRDMA_DEVICE_CTL */
#define PVRDMA_REG_REQUEST    0x10    /* W: Indicate device request. */
#define PVRDMA_REG_ERR        0x14    /* R: Device error. */
#define PVRDMA_REG_ICR        0x18    /* R: Interrupt cause. */
#define PVRDMA_REG_IMR        0x1c    /* R/W: Interrupt mask. */
#define PVRDMA_REG_MACL        0x20    /* R/W: MAC address low. */
#define PVRDMA_REG_MACH        0x24    /* R/W: MAC address high. */

/* Object flags. */
#define PVRDMA_CQ_FLAG_ARMED_SOL    BIT(0)    /* Armed for solicited-only. */
#define PVRDMA_CQ_FLAG_ARMED        BIT(1)    /* Armed. */
#define PVRDMA_MR_FLAG_DMA        BIT(0)    /* DMA region. */
#define PVRDMA_MR_FLAG_FRMR        BIT(1)    /* Fast reg memory region. */

/*
 * Atomic operation capability (masked versions are extended atomic
 * operations.
 */

#define PVRDMA_ATOMIC_OP_COMP_SWAP    BIT(0) /* Compare and swap. */
#define PVRDMA_ATOMIC_OP_FETCH_ADD    BIT(1) /* Fetch and add. */
#define PVRDMA_ATOMIC_OP_MASK_COMP_SWAP    BIT(2) /* Masked compare and swap. */
#define PVRDMA_ATOMIC_OP_MASK_FETCH_ADD    BIT(3) /* Masked fetch and add. */

/*
 * Base Memory Management Extension flags to support Fast Reg Memory Regions
 * and Fast Reg Work Requests. Each flag represents a verb operation and we
 * must support all of them to qualify for the BMME device cap.
 */

#define PVRDMA_BMME_FLAG_LOCAL_INV    BIT(0) /* Local Invalidate. */
#define PVRDMA_BMME_FLAG_REMOTE_INV    BIT(1) /* Remote Invalidate. */
#define PVRDMA_BMME_FLAG_FAST_REG_WR    BIT(2) /* Fast Reg Work Request. */

/*
 * GID types. The interpretation of the gid_types bit field in the device
 * capabilities will depend on the device mode. For now, the device only
 * supports RoCE as mode, so only the different GID types for RoCE are
 * defined.
 */

#define PVRDMA_GID_TYPE_FLAG_ROCE_V1 BIT(0)
#define PVRDMA_GID_TYPE_FLAG_ROCE_V2 BIT(1)

enum pvrdma_pci_resource {
    PVRDMA_PCI_RESOURCE_MSIX,    /* BAR0: MSI-X, MMIO. */
    PVRDMA_PCI_RESOURCE_REG,    /* BAR1: Registers, MMIO. */
    PVRDMA_PCI_RESOURCE_UAR,    /* BAR2: UAR pages, MMIO, 64-bit. */
    PVRDMA_PCI_RESOURCE_LAST,    /* Last. */
};

enum pvrdma_device_ctl {
    PVRDMA_DEVICE_CTL_ACTIVATE,    /* Activate device. */
    PVRDMA_DEVICE_CTL_QUIESCE,    /* Quiesce device. */
    PVRDMA_DEVICE_CTL_RESET,    /* Reset device. */
};

enum pvrdma_intr_vector {
    PVRDMA_INTR_VECTOR_RESPONSE,    /* Command response. */
    PVRDMA_INTR_VECTOR_ASYNC,    /* Async events. */
    PVRDMA_INTR_VECTOR_CQ,        /* CQ notification. */
    /* Additional CQ notification vectors. */
};

enum pvrdma_intr_cause {
    PVRDMA_INTR_CAUSE_RESPONSE    = (1 << PVRDMA_INTR_VECTOR_RESPONSE),
    PVRDMA_INTR_CAUSE_ASYNC        = (1 << PVRDMA_INTR_VECTOR_ASYNC),
    PVRDMA_INTR_CAUSE_CQ        = (1 << PVRDMA_INTR_VECTOR_CQ),
};

enum pvrdma_intr_type {
    PVRDMA_INTR_TYPE_INTX,        /* Legacy. */
    PVRDMA_INTR_TYPE_MSI,        /* MSI. */
    PVRDMA_INTR_TYPE_MSIX,        /* MSI-X. */
};

enum pvrdma_gos_bits {
    PVRDMA_GOS_BITS_UNK,        /* Unknown. */
    PVRDMA_GOS_BITS_32,        /* 32-bit. */
    PVRDMA_GOS_BITS_64,        /* 64-bit. */
};

enum pvrdma_gos_type {
    PVRDMA_GOS_TYPE_UNK,        /* Unknown. */
    PVRDMA_GOS_TYPE_LINUX,        /* Linux. */
};

enum pvrdma_device_mode {
    PVRDMA_DEVICE_MODE_ROCE,    /* RoCE. */
    PVRDMA_DEVICE_MODE_IWARP,    /* iWarp. */
    PVRDMA_DEVICE_MODE_IB,        /* InfiniBand. */
};

struct pvrdma_gos_info {
    u32 gos_bits:2;            /* W: PVRDMA_GOS_BITS_ */
    u32 gos_type:4;            /* W: PVRDMA_GOS_TYPE_ */
    u32 gos_ver:16;            /* W: Guest OS version. */
    u32 gos_misc:10;        /* W: Other. */
    u32 pad;            /* Pad to 8-byte alignment. */
};

struct pvrdma_device_caps {
    u64 fw_ver;                /* R: Query device. */
    __be64 node_guid;
    __be64 sys_image_guid;
    u64 max_mr_size;
    u64 page_size_cap;
    u64 atomic_arg_sizes;            /* EXP verbs. */
    u32 exp_comp_mask;            /* EXP verbs. */
    u32 device_cap_flags2;            /* EXP verbs. */
    u32 max_fa_bit_boundary;        /* EXP verbs. */
    u32 log_max_atomic_inline_arg;        /* EXP verbs. */
    u32 vendor_id;
    u32 vendor_part_id;
    u32 hw_ver;
    u32 max_qp;
    u32 max_qp_wr;
    u32 device_cap_flags;
    u32 max_sge;
    u32 max_sge_rd;
    u32 max_cq;
    u32 max_cqe;
    u32 max_mr;
    u32 max_pd;
    u32 max_qp_rd_atom;
    u32 max_ee_rd_atom;
    u32 max_res_rd_atom;
    u32 max_qp_init_rd_atom;
    u32 max_ee_init_rd_atom;
    u32 max_ee;
    u32 max_rdd;
    u32 max_mw;
    u32 max_raw_ipv6_qp;
    u32 max_raw_ethy_qp;
    u32 max_mcast_grp;
    u32 max_mcast_qp_attach;
    u32 max_total_mcast_qp_attach;
    u32 max_ah;
    u32 max_fmr;
    u32 max_map_per_fmr;
    u32 max_srq;
    u32 max_srq_wr;
    u32 max_srq_sge;
    u32 max_uar;
    u32 gid_tbl_len;
    u16 max_pkeys;
    u8  local_ca_ack_delay;
    u8  phys_port_cnt;
    u8  mode;                /* PVRDMA_DEVICE_MODE_ */
    u8  atomic_ops;                /* PVRDMA_ATOMIC_OP_* bits */
    u8  bmme_flags;                /* FRWR Mem Mgmt Extensions */
    u8  gid_types;                /* PVRDMA_GID_TYPE_FLAG_ */
    u8  reserved[4];
};

struct pvrdma_ring_page_info {
    u32 num_pages;                /* Num pages incl. header. */
    u32 reserved;                /* Reserved. */
    u64 pdir_dma;                /* Page directory PA. */
};

#pragma pack(push, 1)

struct pvrdma_device_shared_region {
    u32 driver_version;            /* W: Driver version. */
    u32 pad;                /* Pad to 8-byte align. */
    struct pvrdma_gos_info gos_info;    /* W: Guest OS information. */
    u64 cmd_slot_dma;            /* W: Command slot address. */
    u64 resp_slot_dma;            /* W: Response slot address. */
    struct pvrdma_ring_page_info async_ring_pages;
                        /* W: Async ring page info. */
    struct pvrdma_ring_page_info cq_ring_pages;
                        /* W: CQ ring page info. */
    u32 uar_pfn;                /* W: UAR pageframe. */
    u32 pad2;                /* Pad to 8-byte align. */
    struct pvrdma_device_caps caps;        /* R: Device capabilities. */
};

#pragma pack(pop)


/* Event types. Currently a 1:1 mapping with enum ib_event. */
enum pvrdma_eqe_type {
    PVRDMA_EVENT_CQ_ERR,
    PVRDMA_EVENT_QP_FATAL,
    PVRDMA_EVENT_QP_REQ_ERR,
    PVRDMA_EVENT_QP_ACCESS_ERR,
    PVRDMA_EVENT_COMM_EST,
    PVRDMA_EVENT_SQ_DRAINED,
    PVRDMA_EVENT_PATH_MIG,
    PVRDMA_EVENT_PATH_MIG_ERR,
    PVRDMA_EVENT_DEVICE_FATAL,
    PVRDMA_EVENT_PORT_ACTIVE,
    PVRDMA_EVENT_PORT_ERR,
    PVRDMA_EVENT_LID_CHANGE,
    PVRDMA_EVENT_PKEY_CHANGE,
    PVRDMA_EVENT_SM_CHANGE,
    PVRDMA_EVENT_SRQ_ERR,
    PVRDMA_EVENT_SRQ_LIMIT_REACHED,
    PVRDMA_EVENT_QP_LAST_WQE_REACHED,
    PVRDMA_EVENT_CLIENT_REREGISTER,
    PVRDMA_EVENT_GID_CHANGE,
};

/* Event queue element. */
struct pvrdma_eqe {
    u32 type;    /* Event type. */
    u32 info;    /* Handle, other. */
};

/* CQ notification queue element. */
struct pvrdma_cqne {
    u32 info;    /* Handle */
};

static inline void pvrdma_init_cqe(struct pvrdma_cqe *cqe, u64 wr_id, u64 qp)
{
    memset(cqe, 0, sizeof(*cqe));
    cqe->status = PVRDMA_WC_GENERAL_ERR;
    cqe->wr_id = wr_id;
    cqe->qp = qp;
}

#endif /* PVRDMA_DEFS_H */
