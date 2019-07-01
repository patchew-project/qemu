/*
 * Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_VIRTIO_MMIO_H
#define QEMU_VIRTIO_MMIO_H

#include "hw/virtio/virtio-bus.h"

/* QOM macros */
/* virtio-mmio-bus */
#define TYPE_VIRTIO_MMIO_BUS "virtio-mmio-bus"
#define VIRTIO_MMIO_BUS(obj) \
        OBJECT_CHECK(VirtioBusState, (obj), TYPE_VIRTIO_MMIO_BUS)
#define VIRTIO_MMIO_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioBusClass, (obj), TYPE_VIRTIO_MMIO_BUS)
#define VIRTIO_MMIO_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioBusClass, (klass), TYPE_VIRTIO_MMIO_BUS)

/* virtio-mmio */
#define TYPE_VIRTIO_MMIO "virtio-mmio"
#define VIRTIO_MMIO(obj) \
        OBJECT_CHECK(VirtIOMMIOProxy, (obj), TYPE_VIRTIO_MMIO)

#define VIRT_MAGIC 0x74726976 /* 'virt' */
#define VIRT_VERSION 1
#define VIRT_VENDOR 0x554D4551 /* 'QEMU' */

typedef struct {
    /* Generic */
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    /* Guest accessible state needing migration and reset */
    uint32_t host_features_sel;
    uint32_t guest_features_sel;
    uint32_t guest_page_shift;
    /* virtio-bus */
    VirtioBusState bus;
    bool format_transport_address;
} VirtIOMMIOProxy;

#endif
