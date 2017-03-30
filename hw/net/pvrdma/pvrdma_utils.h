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

#ifndef PVRDMA_UTILS_H
#define PVRDMA_UTILS_H

#define pr_info(fmt, ...) \
    fprintf(stdout, "%s: %-20s (%3d): " fmt, "pvrdma",  __func__, __LINE__,\
           ## __VA_ARGS__)

#define pr_err(fmt, ...) \
    fprintf(stderr, "%s: Error at %-20s (%3d): " fmt, "pvrdma", __func__, \
        __LINE__, ## __VA_ARGS__)

#define DEBUG
#ifdef DEBUG
#define pr_dbg(fmt, ...) \
    fprintf(stdout, "%s: %-20s (%3d): " fmt, "pvrdma", __func__, __LINE__,\
           ## __VA_ARGS__)
#else
#define pr_dbg(fmt, ...)
#endif

static inline int roundup_pow_of_two(int x)
{
    x--;
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x + 1;
}

void pvrdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len);
void *pvrdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen);

#endif
