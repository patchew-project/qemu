/*
 * NVMe VFIO interface
 *
 * Copyright 2016 Red Hat, Inc.
 *
 * Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_VFIO_H
#define QEMU_VFIO_H
#include "qemu/queue.h"

typedef struct NVMeVFIOState NVMeVFIOState;

NVMeVFIOState *nvme_vfio_open_pci(const char *device, Error **errp);
void nvme_vfio_reset(NVMeVFIOState *s);
void nvme_vfio_close(NVMeVFIOState *s);
int nvme_vfio_dma_map(NVMeVFIOState *s, void *host, size_t size,
                      bool temporary, uint64_t *iova_list);
int nvme_vfio_dma_reset_temporary(NVMeVFIOState *s);
void nvme_vfio_dma_unmap(NVMeVFIOState *s, void *host);
void *nvme_vfio_pci_map_bar(NVMeVFIOState *s, int index, Error **errp);
void nvme_vfio_pci_unmap_bar(NVMeVFIOState *s, int index, void *bar);
int nvme_vfio_pci_init_irq(NVMeVFIOState *s, EventNotifier *e,
                           int irq_type, Error **errp);

#endif
