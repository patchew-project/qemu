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

#ifndef PVRDMA_KDBR_H
#define PVRDMA_KDBR_H

#include <hw/net/pvrdma/pvrdma_types.h>
#include <hw/net/pvrdma/pvrdma_ib_verbs.h>
#include <hw/net/pvrdma/pvrdma_rm.h>
#include <hw/net/pvrdma/kdbr.h>

typedef struct KdbrCompThread {
    QemuThread thread;
    QemuMutex mutex;
    bool run;
} KdbrCompThread;

typedef struct KdbrPort {
    int num;
    int fd;
    KdbrCompThread comp_thread;
    PCIDevice *dev;
} KdbrPort;

int kdbr_init(void);
void kdbr_fini(void);
KdbrPort *kdbr_alloc_port(PVRDMADev *dev);
void kdbr_free_port(KdbrPort *port);
void kdbr_register_tx_comp_handler(void (*comp_handler)(int status,
                                   unsigned int vendor_err, void *ctx));
void kdbr_register_rx_comp_handler(void (*comp_handler)(int status,
                                   unsigned int vendor_err, void *ctx));
unsigned long kdbr_open_connection(KdbrPort *port, u32 qpn,
                                   union pvrdma_gid dgid, u32 dqpn,
                                   bool rc_qp);
void kdbr_close_connection(KdbrPort *port, unsigned long connection_id);
void kdbr_send_wqe(KdbrPort *port, unsigned long connection_id, bool rc_qp,
                   struct RmSqWqe *wqe, void *ctx);
void kdbr_recv_wqe(KdbrPort *port, unsigned long connection_id,
                   struct RmRqWqe *wqe, void *ctx);

#endif
