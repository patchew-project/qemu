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

#ifndef PVRDMA_UAPI_H
#define PVRDMA_UAPI_H

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include <hw/net/pvrdma/pvrdma_types.h>
#include <qemu/compiler.h>
#include <qemu/atomic.h>

#define PVRDMA_VERSION 17

#define PVRDMA_UAR_HANDLE_MASK    0x00FFFFFF    /* Bottom 24 bits. */
#define PVRDMA_UAR_QP_OFFSET    0        /* Offset of QP doorbell. */
#define PVRDMA_UAR_QP_SEND    BIT(30)        /* Send bit. */
#define PVRDMA_UAR_QP_RECV    BIT(31)        /* Recv bit. */
#define PVRDMA_UAR_CQ_OFFSET    4        /* Offset of CQ doorbell. */
#define PVRDMA_UAR_CQ_ARM_SOL    BIT(29)        /* Arm solicited bit. */
#define PVRDMA_UAR_CQ_ARM    BIT(30)        /* Arm bit. */
#define PVRDMA_UAR_CQ_POLL    BIT(31)        /* Poll bit. */
#define PVRDMA_INVALID_IDX    -1        /* Invalid index. */

/* PVRDMA atomic compare and swap */
struct pvrdma_exp_cmp_swap {
    __u64 swap_val;
    __u64 compare_val;
    __u64 swap_mask;
    __u64 compare_mask;
};

/* PVRDMA atomic fetch and add */
struct pvrdma_exp_fetch_add {
    __u64 add_val;
    __u64 field_boundary;
};

/* PVRDMA address vector. */
struct pvrdma_av {
    __u32 port_pd;
    __u32 sl_tclass_flowlabel;
    __u8 dgid[16];
    __u8 src_path_bits;
    __u8 gid_index;
    __u8 stat_rate;
    __u8 hop_limit;
    __u8 dmac[6];
    __u8 reserved[6];
};

/* PVRDMA scatter/gather entry */
struct pvrdma_sge {
    __u64   addr;
    __u32   length;
    __u32   lkey;
};

/* PVRDMA receive queue work request */
struct pvrdma_rq_wqe_hdr {
    __u64 wr_id;        /* wr id */
    __u32 num_sge;        /* size of s/g array */
    __u32 total_len;    /* reserved */
};
/* Use pvrdma_sge (ib_sge) for receive queue s/g array elements. */

/* PVRDMA send queue work request */
struct pvrdma_sq_wqe_hdr {
    __u64 wr_id;        /* wr id */
    __u32 num_sge;        /* size of s/g array */
    __u32 total_len;    /* reserved */
    __u32 opcode;        /* operation type */
    __u32 send_flags;    /* wr flags */
    union {
        __u32 imm_data;
        __u32 invalidate_rkey;
    } ex;
    __u32 reserved;
    union {
        struct {
            __u64 remote_addr;
            __u32 rkey;
            __u8 reserved[4];
        } rdma;
        struct {
            __u64 remote_addr;
            __u64 compare_add;
            __u64 swap;
            __u32 rkey;
            __u32 reserved;
        } atomic;
        struct {
            __u64 remote_addr;
            __u32 log_arg_sz;
            __u32 rkey;
            union {
                struct pvrdma_exp_cmp_swap  cmp_swap;
                struct pvrdma_exp_fetch_add fetch_add;
            } wr_data;
        } masked_atomics;
        struct {
            __u64 iova_start;
            __u64 pl_pdir_dma;
            __u32 page_shift;
            __u32 page_list_len;
            __u32 length;
            __u32 access_flags;
            __u32 rkey;
        } fast_reg;
        struct {
            __u32 remote_qpn;
            __u32 remote_qkey;
            struct pvrdma_av av;
        } ud;
    } wr;
};
/* Use pvrdma_sge (ib_sge) for send queue s/g array elements. */

/* Completion queue element. */
struct pvrdma_cqe {
    __u64 wr_id;
    __u64 qp;
    __u32 opcode;
    __u32 status;
    __u32 byte_len;
    __u32 imm_data;
    __u32 src_qp;
    __u32 wc_flags;
    __u32 vendor_err;
    __u16 pkey_index;
    __u16 slid;
    __u8 sl;
    __u8 dlid_path_bits;
    __u8 port_num;
    __u8 smac[6];
    __u8 reserved2[7]; /* Pad to next power of 2 (64). */
};

struct pvrdma_ring {
    int prod_tail;    /* Producer tail. */
    int cons_head;    /* Consumer head. */
};

struct pvrdma_ring_state {
    struct pvrdma_ring tx;    /* Tx ring. */
    struct pvrdma_ring rx;    /* Rx ring. */
};

static inline int pvrdma_idx_valid(__u32 idx, __u32 max_elems)
{
    /* Generates fewer instructions than a less-than. */
    return (idx & ~((max_elems << 1) - 1)) == 0;
}

static inline __s32 pvrdma_idx(int *var, __u32 max_elems)
{
    unsigned int idx = atomic_read(var);

    if (pvrdma_idx_valid(idx, max_elems)) {
        return idx & (max_elems - 1);
    }
    return PVRDMA_INVALID_IDX;
}

static inline void pvrdma_idx_ring_inc(int *var, __u32 max_elems)
{
    __u32 idx = atomic_read(var) + 1;    /* Increment. */

    idx &= (max_elems << 1) - 1;        /* Modulo size, flip gen. */
    atomic_set(var, idx);
}

static inline __s32 pvrdma_idx_ring_has_space(const struct pvrdma_ring *r,
                          __u32 max_elems, __u32 *out_tail)
{
    const __u32 tail = atomic_read(&r->prod_tail);
    const __u32 head = atomic_read(&r->cons_head);

    if (pvrdma_idx_valid(tail, max_elems) &&
        pvrdma_idx_valid(head, max_elems)) {
        *out_tail = tail & (max_elems - 1);
        return tail != (head ^ max_elems);
    }
    return PVRDMA_INVALID_IDX;
}

static inline __s32 pvrdma_idx_ring_has_data(const struct pvrdma_ring *r,
                         __u32 max_elems, __u32 *out_head)
{
    const __u32 tail = atomic_read(&r->prod_tail);
    const __u32 head = atomic_read(&r->cons_head);

    if (pvrdma_idx_valid(tail, max_elems) &&
        pvrdma_idx_valid(head, max_elems)) {
        *out_head = head & (max_elems - 1);
        return tail != head;
    }
    return PVRDMA_INVALID_IDX;
}

static inline bool pvrdma_idx_ring_is_valid_idx(const struct pvrdma_ring *r,
                        __u32 max_elems, __u32 *idx)
{
    const __u32 tail = atomic_read(&r->prod_tail);
    const __u32 head = atomic_read(&r->cons_head);

    if (pvrdma_idx_valid(tail, max_elems) &&
        pvrdma_idx_valid(head, max_elems) &&
        pvrdma_idx_valid(*idx, max_elems)) {
        if (tail > head && (*idx < tail && *idx >= head)) {
            return true;
        } else if (head > tail && (*idx >= head || *idx < tail)) {
            return true;
        }
    }
    return false;
}

#endif /* PVRDMA_UAPI_H */
