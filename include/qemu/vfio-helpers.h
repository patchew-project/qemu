/*
 * VFIO helper functions
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

typedef struct QEMUVFIOState QEMUVFIOState;

QEMUVFIOState *qemu_vfio_open_pci(const char *device, Error **errp);
void qemu_vfio_close(QEMUVFIOState *s);
int qemu_vfio_dma_map(QEMUVFIOState *s, void *host, size_t size,
                      bool contiguous, uint64_t *iova_list);
void qemu_vfio_dma_unmap(QEMUVFIOState *s, void *host);
void *qemu_vfio_pci_map_bar(QEMUVFIOState *s, int index, Error **errp);
void qemu_vfio_pci_unmap_bar(QEMUVFIOState *s, int index, void *bar);
int qemu_vfio_pci_init_irq(QEMUVFIOState *s, EventNotifier *e,
                           int irq_type, Error **errp);

#endif
