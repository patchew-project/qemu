#ifndef VIRTIO_ACPI_H
#define VIRTIO_ACPI_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"

void virtio_acpi_dsdt_add(Aml *scope, const MemMapEntry *virtio_mmio_memmap,
                          uint32_t mmio_irq, int num);

#endif

