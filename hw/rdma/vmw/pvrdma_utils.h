/*
 * QEMU VMWARE paravirtual RDMA address mapping utilities
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

#ifndef PVRDMA_UTILS_H
#define PVRDMA_UTILS_H

#include <include/hw/pci/pci.h>

void pvrdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len);
void *pvrdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen);
void *pvrdma_map_to_pdir(PCIDevice *pdev, uint64_t pdir_dma, uint32_t nchunks,
                         size_t length);

#endif
