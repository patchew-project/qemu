/*
 * QEMU VMWARE paravirtual RDMA interface definitions
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

#ifndef PVRDMA_PVRDMA_H
#define PVRDMA_PVRDMA_H

#include <qemu/osdep.h>
#include <hw/pci/pci.h>
#include <hw/pci/msix.h>
#include <hw/net/pvrdma/pvrdma_kdbr.h>
#include <hw/net/pvrdma/pvrdma_rm.h>
#include <hw/net/pvrdma/pvrdma_defs.h>
#include <hw/net/pvrdma/pvrdma_dev_api.h>
#include <hw/net/pvrdma/pvrdma_ring.h>

/* BARs */
#define RDMA_MSIX_BAR_IDX    0
#define RDMA_REG_BAR_IDX     1
#define RDMA_UAR_BAR_IDX     2
#define RDMA_BAR0_MSIX_SIZE  (16 * 1024)
#define RDMA_BAR1_REGS_SIZE  256
#define RDMA_BAR2_UAR_SIZE   (16 * 1024)

/* MSIX */
#define RDMA_MAX_INTRS       3
#define RDMA_MSIX_TABLE      0x0000
#define RDMA_MSIX_PBA        0x2000

/* Interrupts Vectors */
#define INTR_VEC_CMD_RING            0
#define INTR_VEC_CMD_ASYNC_EVENTS    1
#define INTR_VEC_CMD_COMPLETION_Q    2

/* HW attributes */
#define PVRDMA_HW_NAME       "pvrdma"
#define PVRDMA_HW_VERSION    17
#define PVRDMA_FW_VERSION    14

/* Vendor Errors, codes 100 to FFF kept for kdbr */
#define VENDOR_ERR_TOO_MANY_SGES    0x201
#define VENDOR_ERR_NOMEM            0x202
#define VENDOR_ERR_FAIL_KDBR        0x203

typedef struct HWResourceIDs {
    unsigned long *local_bitmap;
    __u32 *hw_map;
} HWResourceIDs;

typedef struct DSRInfo {
    dma_addr_t dma;
    struct pvrdma_device_shared_region *dsr;

    union pvrdma_cmd_req *req;
    union pvrdma_cmd_resp *rsp;

    struct pvrdma_ring *async_ring_state;
    Ring async;

    struct pvrdma_ring *cq_ring_state;
    Ring cq;
} DSRInfo;

typedef struct PVRDMADev {
    PCIDevice parent_obj;
    MemoryRegion msix;
    MemoryRegion regs;
    __u32 regs_data[RDMA_BAR1_REGS_SIZE];
    MemoryRegion uar;
    __u32 uar_data[RDMA_BAR2_UAR_SIZE];
    DSRInfo dsr_info;
    int interrupt_mask;
    RmPort ports[MAX_PORTS];
    u64 sys_image_guid;
    u64 node_guid;
    u64 network_prefix;
    RmResTbl pd_tbl;
    RmResTbl mr_tbl;
    RmResTbl qp_tbl;
    RmResTbl cq_tbl;
    RmResTbl wqe_ctx_tbl;
} PVRDMADev;
#define PVRDMA_DEV(dev) OBJECT_CHECK(PVRDMADev, (dev), PVRDMA_HW_NAME)

static inline int get_reg_val(PVRDMADev *dev, hwaddr addr, __u32 *val)
{
    int idx = addr >> 2;

    if (idx > RDMA_BAR1_REGS_SIZE) {
        return -EINVAL;
    }

    *val = dev->regs_data[idx];

    return 0;
}
static inline int set_reg_val(PVRDMADev *dev, hwaddr addr, __u32 val)
{
    int idx = addr >> 2;

    if (idx > RDMA_BAR1_REGS_SIZE) {
        return -EINVAL;
    }

    dev->regs_data[idx] = val;

    return 0;
}
static inline int get_uar_val(PVRDMADev *dev, hwaddr addr, __u32 *val)
{
    int idx = addr >> 2;

    if (idx > RDMA_BAR2_UAR_SIZE) {
        return -EINVAL;
    }

    *val = dev->uar_data[idx];

    return 0;
}
static inline int set_uar_val(PVRDMADev *dev, hwaddr addr, __u32 val)
{
    int idx = addr >> 2;

    if (idx > RDMA_BAR2_UAR_SIZE) {
        return -EINVAL;
    }

    dev->uar_data[idx] = val;

    return 0;
}

static inline void post_interrupt(PVRDMADev *dev, unsigned vector)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);

    if (likely(dev->interrupt_mask == 0)) {
        msix_notify(pci_dev, vector);
    }
}

int execute_command(PVRDMADev *dev);

#endif
