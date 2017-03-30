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

#ifndef PVRDMA_DEV_API_H
#define PVRDMA_DEV_API_H

#include <hw/net/pvrdma/pvrdma_types.h>
#include <hw/net/pvrdma/pvrdma_ib_verbs.h>

enum {
    PVRDMA_CMD_FIRST,
    PVRDMA_CMD_QUERY_PORT = PVRDMA_CMD_FIRST,
    PVRDMA_CMD_QUERY_PKEY,
    PVRDMA_CMD_CREATE_PD,
    PVRDMA_CMD_DESTROY_PD,
    PVRDMA_CMD_CREATE_MR,
    PVRDMA_CMD_DESTROY_MR,
    PVRDMA_CMD_CREATE_CQ,
    PVRDMA_CMD_RESIZE_CQ,
    PVRDMA_CMD_DESTROY_CQ,
    PVRDMA_CMD_CREATE_QP,
    PVRDMA_CMD_MODIFY_QP,
    PVRDMA_CMD_QUERY_QP,
    PVRDMA_CMD_DESTROY_QP,
    PVRDMA_CMD_CREATE_UC,
    PVRDMA_CMD_DESTROY_UC,
    PVRDMA_CMD_CREATE_BIND,
    PVRDMA_CMD_DESTROY_BIND,
    PVRDMA_CMD_MAX,
};

enum {
    PVRDMA_CMD_FIRST_RESP = (1 << 31),
    PVRDMA_CMD_QUERY_PORT_RESP = PVRDMA_CMD_FIRST_RESP,
    PVRDMA_CMD_QUERY_PKEY_RESP,
    PVRDMA_CMD_CREATE_PD_RESP,
    PVRDMA_CMD_DESTROY_PD_RESP_NOOP,
    PVRDMA_CMD_CREATE_MR_RESP,
    PVRDMA_CMD_DESTROY_MR_RESP_NOOP,
    PVRDMA_CMD_CREATE_CQ_RESP,
    PVRDMA_CMD_RESIZE_CQ_RESP,
    PVRDMA_CMD_DESTROY_CQ_RESP_NOOP,
    PVRDMA_CMD_CREATE_QP_RESP,
    PVRDMA_CMD_MODIFY_QP_RESP,
    PVRDMA_CMD_QUERY_QP_RESP,
    PVRDMA_CMD_DESTROY_QP_RESP,
    PVRDMA_CMD_CREATE_UC_RESP,
    PVRDMA_CMD_DESTROY_UC_RESP_NOOP,
    PVRDMA_CMD_CREATE_BIND_RESP_NOOP,
    PVRDMA_CMD_DESTROY_BIND_RESP_NOOP,
    PVRDMA_CMD_MAX_RESP,
};

struct pvrdma_cmd_hdr {
    u64 response;        /* Key for response lookup. */
    u32 cmd;        /* PVRDMA_CMD_ */
    u32 reserved;        /* Reserved. */
};

struct pvrdma_cmd_resp_hdr {
    u64 response;        /* From cmd hdr. */
    u32 ack;        /* PVRDMA_CMD_XXX_RESP */
    u8 err;            /* Error. */
    u8 reserved[3];        /* Reserved. */
};

struct pvrdma_cmd_query_port {
    struct pvrdma_cmd_hdr hdr;
    u8 port_num;
    u8 reserved[7];
};

struct pvrdma_cmd_query_port_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    struct pvrdma_port_attr attrs;
};

struct pvrdma_cmd_query_pkey {
    struct pvrdma_cmd_hdr hdr;
    u8 port_num;
    u8 index;
    u8 reserved[6];
};

struct pvrdma_cmd_query_pkey_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u16 pkey;
    u8 reserved[6];
};

struct pvrdma_cmd_create_uc {
    struct pvrdma_cmd_hdr hdr;
    u32 pfn; /* UAR page frame number */
    u8 reserved[4];
};

struct pvrdma_cmd_create_uc_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 ctx_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_destroy_uc {
    struct pvrdma_cmd_hdr hdr;
    u32 ctx_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_create_pd {
    struct pvrdma_cmd_hdr hdr;
    u32 ctx_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_create_pd_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 pd_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_destroy_pd {
    struct pvrdma_cmd_hdr hdr;
    u32 pd_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_create_mr {
    struct pvrdma_cmd_hdr hdr;
    u64 start;
    u64 length;
    u64 pdir_dma;
    u32 pd_handle;
    u32 access_flags;
    u32 flags;
    u32 nchunks;
};

struct pvrdma_cmd_create_mr_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 mr_handle;
    u32 lkey;
    u32 rkey;
    u8 reserved[4];
};

struct pvrdma_cmd_destroy_mr {
    struct pvrdma_cmd_hdr hdr;
    u32 mr_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_create_cq {
    struct pvrdma_cmd_hdr hdr;
    u64 pdir_dma;
    u32 ctx_handle;
    u32 cqe;
    u32 nchunks;
    u8 reserved[4];
};

struct pvrdma_cmd_create_cq_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 cq_handle;
    u32 cqe;
};

struct pvrdma_cmd_resize_cq {
    struct pvrdma_cmd_hdr hdr;
    u32 cq_handle;
    u32 cqe;
};

struct pvrdma_cmd_resize_cq_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 cqe;
    u8 reserved[4];
};

struct pvrdma_cmd_destroy_cq {
    struct pvrdma_cmd_hdr hdr;
    u32 cq_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_create_qp {
    struct pvrdma_cmd_hdr hdr;
    u64 pdir_dma;
    u32 pd_handle;
    u32 send_cq_handle;
    u32 recv_cq_handle;
    u32 srq_handle;
    u32 max_send_wr;
    u32 max_recv_wr;
    u32 max_send_sge;
    u32 max_recv_sge;
    u32 max_inline_data;
    u32 lkey;
    u32 access_flags;
    u16 total_chunks;
    u16 send_chunks;
    u16 max_atomic_arg;
    u8 sq_sig_all;
    u8 qp_type;
    u8 is_srq;
    u8 reserved[3];
};

struct pvrdma_cmd_create_qp_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 qpn;
    u32 max_send_wr;
    u32 max_recv_wr;
    u32 max_send_sge;
    u32 max_recv_sge;
    u32 max_inline_data;
};

struct pvrdma_cmd_modify_qp {
    struct pvrdma_cmd_hdr hdr;
    u32 qp_handle;
    u32 attr_mask;
    struct pvrdma_qp_attr attrs;
};

struct pvrdma_cmd_query_qp {
    struct pvrdma_cmd_hdr hdr;
    u32 qp_handle;
    u32 attr_mask;
};

struct pvrdma_cmd_query_qp_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    struct pvrdma_qp_attr attrs;
};

struct pvrdma_cmd_destroy_qp {
    struct pvrdma_cmd_hdr hdr;
    u32 qp_handle;
    u8 reserved[4];
};

struct pvrdma_cmd_destroy_qp_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    u32 events_reported;
    u8 reserved[4];
};

struct pvrdma_cmd_create_bind {
    struct pvrdma_cmd_hdr hdr;
    u32 mtu;
    u32 vlan;
    u32 index;
    u8 new_gid[16];
    u8 gid_type;
    u8 reserved[3];
};

struct pvrdma_cmd_destroy_bind {
    struct pvrdma_cmd_hdr hdr;
    u32 index;
    u8 dest_gid[16];
    u8 reserved[4];
};

union pvrdma_cmd_req {
    struct pvrdma_cmd_hdr hdr;
    struct pvrdma_cmd_query_port query_port;
    struct pvrdma_cmd_query_pkey query_pkey;
    struct pvrdma_cmd_create_uc create_uc;
    struct pvrdma_cmd_destroy_uc destroy_uc;
    struct pvrdma_cmd_create_pd create_pd;
    struct pvrdma_cmd_destroy_pd destroy_pd;
    struct pvrdma_cmd_create_mr create_mr;
    struct pvrdma_cmd_destroy_mr destroy_mr;
    struct pvrdma_cmd_create_cq create_cq;
    struct pvrdma_cmd_resize_cq resize_cq;
    struct pvrdma_cmd_destroy_cq destroy_cq;
    struct pvrdma_cmd_create_qp create_qp;
    struct pvrdma_cmd_modify_qp modify_qp;
    struct pvrdma_cmd_query_qp query_qp;
    struct pvrdma_cmd_destroy_qp destroy_qp;
    struct pvrdma_cmd_create_bind create_bind;
    struct pvrdma_cmd_destroy_bind destroy_bind;
};

union pvrdma_cmd_resp {
    struct pvrdma_cmd_resp_hdr hdr;
    struct pvrdma_cmd_query_port_resp query_port_resp;
    struct pvrdma_cmd_query_pkey_resp query_pkey_resp;
    struct pvrdma_cmd_create_uc_resp create_uc_resp;
    struct pvrdma_cmd_create_pd_resp create_pd_resp;
    struct pvrdma_cmd_create_mr_resp create_mr_resp;
    struct pvrdma_cmd_create_cq_resp create_cq_resp;
    struct pvrdma_cmd_resize_cq_resp resize_cq_resp;
    struct pvrdma_cmd_create_qp_resp create_qp_resp;
    struct pvrdma_cmd_query_qp_resp query_qp_resp;
    struct pvrdma_cmd_destroy_qp_resp destroy_qp_resp;
};

#endif /* PVRDMA_DEV_API_H */
