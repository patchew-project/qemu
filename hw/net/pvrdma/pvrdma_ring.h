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

#ifndef PVRDMA_RING_H
#define PVRDMA_RING_H

#include <qemu/typedefs.h>
#include <hw/net/pvrdma/pvrdma-uapi.h>
#include <hw/net/pvrdma/pvrdma_types.h>

#define MAX_RING_NAME_SZ 16

typedef struct Ring {
    char name[MAX_RING_NAME_SZ];
    PCIDevice *dev;
    size_t max_elems;
    size_t elem_sz;
    struct pvrdma_ring *ring_state;
    int npages;
    void **pages;
} Ring;

int ring_init(Ring *ring, const char *name, PCIDevice *dev,
              struct pvrdma_ring *ring_state, size_t max_elems, size_t elem_sz,
              dma_addr_t *tbl, dma_addr_t npages);
void *ring_next_elem_read(Ring *ring);
void ring_read_inc(Ring *ring);
void *ring_next_elem_write(Ring *ring);
void ring_write_inc(Ring *ring);
void ring_free(Ring *ring);

#endif
