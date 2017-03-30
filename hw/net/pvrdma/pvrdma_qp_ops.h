/*
 * QEMU VMWARE paravirtual RDMA QP Operations
 *
 * Developed by Oracle & Redhat
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

typedef struct PVRDMADev PVRDMADev;

int qp_ops_init(void);
void qp_ops_fini(void);
int qp_send(PVRDMADev *dev, __u32 qp_handle);
int qp_recv(PVRDMADev *dev, __u32 qp_handle);

#endif
