/*
 * QEMU VMWARE paravirtual RDMA QP Operations
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PVRDMA_QP_H
#define PVRDMA_QP_H

#include "pvrdma.h"

int pvrdma_qp_ops_init(void);
void pvrdma_qp_ops_fini(void);
int pvrdma_qp_send(PVRDMADev *dev, __u32 qp_handle);
int pvrdma_qp_recv(PVRDMADev *dev, __u32 qp_handle);
void pvrdma_cq_poll(RdmaDeviceResources *dev_res, __u32 cq_handle);

#endif
