/*
 * Model of a virtio-msg AMP capable PCI device.
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"

#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msix.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#include "hw/virtio/virtio-msg.h"
#include "hw/virtio/virtio-msg-bus.h"
#include "hw/virtio/spsc_queue.h"

#define TYPE_VMSG_AMP_PCI "virtio-msg-amp-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VmsgAmpPciState, VMSG_AMP_PCI)

#define TYPE_VMSG_BUS_AMP_PCI "virtio-msg-bus-amp-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VmsgBusAmpPciState, VMSG_BUS_AMP_PCI)
#define VMSG_BUS_AMP_PCI_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VMSG_BUS_AMP_PCI)

REG32(VERSION,  0x00)
REG32(FEATURES, 0x04)
REG32(NOTIFY,   0x20)

#define MAX_FIFOS 8

typedef struct VmsgBusAmpPciState {
    VirtIOMSGBusDevice parent;
    PCIDevice *pcidev;
    unsigned int queue_index;

    struct {
        void *va;
        spsc_queue driver;
        spsc_queue device;
        unsigned int mapcount;
    } shm;
} VmsgBusAmpPciState;

typedef struct VmsgAmpPciState {
    PCIDevice dev;
    MemoryRegion mr_mmio;
    MemoryRegion mr_ram;

    struct fifo_bus {
        VmsgBusAmpPciState dev;
        VirtIOMSGProxy proxy;
        BusState bus;
    } fifo[MAX_FIFOS];

    struct {
        uint32_t num_fifos;
    } cfg;
} VmsgAmpPciState;

static void vmsg_bus_amp_pci_process(VirtIOMSGBusDevice *bd);

static uint64_t vmsg_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint64_t r = 0;

    assert(size == 4);

    switch (addr) {
    case A_VERSION:
        /* v0.1 */
        r = 0x0001;
        break;
    case A_FEATURES:
        /* No features bit yet.  */
        break;
    default:
        break;
    }

    return r;
}

static void vmsg_write(void *opaque, hwaddr addr, uint64_t val,
                       unsigned int size)
{
    VmsgAmpPciState *s = VMSG_AMP_PCI(opaque);
    unsigned int q;

    assert(size == 4);

    if (addr >= A_NOTIFY) {
        q = (addr - A_NOTIFY) / 4;
        if (q >= s->cfg.num_fifos) {
            /* Fifo doesn't exist.  */
            return;
        }

        vmsg_bus_amp_pci_process(VIRTIO_MSG_BUS_DEVICE(&s->fifo[q].dev));
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only reg 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps vmsg_pci_ops = {
    .read = vmsg_read,
    .write = vmsg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void vmsg_create_bus(VmsgAmpPciState *s, unsigned int i)
{
    DeviceState *dev = DEVICE(s);
    Object *o = OBJECT(s);
    struct fifo_bus *fifo = &s->fifo[i];
    g_autofree char *fifo_name = g_strdup_printf("fifo%d", i);

    qbus_init(&fifo->bus, sizeof(fifo->bus), TYPE_VIRTIO_MSG_OUTER_BUS,
              dev, fifo_name);

    /* Create the proxy.  */
    object_initialize_child(o, "proxy[*]", &fifo->proxy, TYPE_VIRTIO_MSG);
    qdev_realize(DEVICE(&fifo->proxy), BUS(&fifo->bus), &error_fatal);

    object_initialize_child(o, "vmsg[*]", &fifo->dev,
                            TYPE_VMSG_BUS_AMP_PCI);
    qdev_realize(DEVICE(&fifo->dev), &fifo->proxy.msg_bus, &error_fatal);

    msix_vector_use(PCI_DEVICE(s), i);

    /* Caches for quick lookup. */
    fifo->dev.queue_index = i;
    fifo->dev.pcidev = PCI_DEVICE(s);
}

static void vmsg_amp_pci_realizefn(PCIDevice *dev, Error **errp)
{
    VmsgAmpPciState *s = VMSG_AMP_PCI(dev);
    int i;

    if (!s->cfg.num_fifos || s->cfg.num_fifos > MAX_FIFOS) {
        error_setg(errp, "Unsupported number of FIFOs (%u)", s->cfg.num_fifos);
        return;
    }

    memory_region_init_io(&s->mr_mmio, OBJECT(s), &vmsg_pci_ops, s,
                          TYPE_VMSG_AMP_PCI, 16 * KiB);

    /* 16KB per FIFO.  */
    memory_region_init_ram(&s->mr_ram, OBJECT(s), "ram",
                           s->cfg.num_fifos * 16 * KiB, &error_fatal);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mr_mmio);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY |
                             PCI_BASE_ADDRESS_MEM_PREFETCH,
                             &s->mr_ram);

    msix_init_exclusive_bar(PCI_DEVICE(s), s->cfg.num_fifos, 2, &error_fatal);
    for (i = 0; i < s->cfg.num_fifos; i++) {
        vmsg_create_bus(s, i);
    }
}

static const Property vmsg_properties[] = {
    DEFINE_PROP_UINT32("num-fifos", VmsgAmpPciState, cfg.num_fifos, 1),
};

static const VMStateDescription vmstate_vmsg_pci = {
    .name = TYPE_VMSG_AMP_PCI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, VmsgAmpPciState),
        /* TODO: Add all the sub-devs.  */
        VMSTATE_END_OF_LIST()
    }
};

static void vmsg_amp_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, vmsg_properties);

    pc->realize = vmsg_amp_pci_realizefn;
    pc->vendor_id = PCI_VENDOR_ID_XILINX;
    pc->device_id = 0x9039;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_SYSTEM_OTHER;
    dc->vmsd = &vmstate_vmsg_pci;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static bool vmsg_bus_amp_pci_map_fifo(VmsgBusAmpPciState *s)
{
    VmsgAmpPciState *pci_s = VMSG_AMP_PCI(s->pcidev);
    void *va;

    if (s->shm.mapcount) {
        s->shm.mapcount++;
        return true;
    }

    va = memory_region_get_ram_ptr(&pci_s->mr_ram);
    if (!va) {
        return false;
    }

    if (!s->shm.driver.shm) {
        int capacity = spsc_capacity(4 * KiB);

        /*
         * Layout:
         * 0     - 4KB    Reserved
         * 4KB   - 8KB    Driver queue
         * 8KB   - 12KB   Device queue
         */
        spsc_init(&s->shm.driver, "driver", capacity, va + 4 * KiB);
        spsc_init(&s->shm.device, "device", capacity, va + 8 * KiB);
    }

    /* Map queues.  */
    s->shm.va = va;
    s->shm.mapcount++;
    return true;
}

static void vmsg_bus_amp_pci_unmap_fifo(VmsgBusAmpPciState *s)
{
    assert(s->shm.mapcount);
    if (--s->shm.mapcount) {
        return;
    }

    /* TODO: Actually unmap. */
}

static void vmsg_bus_amp_pci_process(VirtIOMSGBusDevice *bd)
{
    VmsgBusAmpPciState *s = VMSG_BUS_AMP_PCI(bd);
    spsc_queue *q;
    VirtIOMSG msg;
    bool r;

    if (!vmsg_bus_amp_pci_map_fifo(s)) {
        return;
    }

    /*
     * We process the opposite queue, i.e, a driver will want to receive
     * messages on the backend queue (and send messages on the driver queue).
     */
    q = bd->peer->is_driver ? &s->shm.device : &s->shm.driver;
    do {
        r = spsc_recv(q, &msg, sizeof msg);
        if (r) {
            virtio_msg_bus_receive(bd, &msg);
        }
    } while (r);
    vmsg_bus_amp_pci_unmap_fifo(s);
}

static int vmsg_bus_amp_pci_send(VirtIOMSGBusDevice *bd, VirtIOMSG *msg_req)
{
    VmsgAmpPciState *pci_s = VMSG_AMP_PCI(OBJECT(bd)->parent);
    VmsgBusAmpPciState *s = VMSG_BUS_AMP_PCI(bd);

    if (!vmsg_bus_amp_pci_map_fifo(s)) {
        return VIRTIO_MSG_ERROR_MEMORY;
    }

    spsc_send(&s->shm.device, msg_req, sizeof *msg_req);

    /* Notify.  */
    msix_notify(PCI_DEVICE(pci_s), s->queue_index);

    vmsg_bus_amp_pci_unmap_fifo(s);
    return VIRTIO_MSG_NO_ERROR;
}

static void vmsg_bus_amp_pci_class_init(ObjectClass *klass,
                                              const void *data)
{
    VirtIOMSGBusDeviceClass *bdc = VIRTIO_MSG_BUS_DEVICE_CLASS(klass);

    bdc->process = vmsg_bus_amp_pci_process;
    bdc->send = vmsg_bus_amp_pci_send;
}

static const TypeInfo vmsg_pci_info[] = {
    {
        .name = TYPE_VMSG_AMP_PCI,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(VmsgAmpPciState),
        .class_init = vmsg_amp_pci_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { }
        },
    }, {
        .name = TYPE_VMSG_BUS_AMP_PCI,
        .parent = TYPE_VIRTIO_MSG_BUS_DEVICE,
        .instance_size = sizeof(VmsgBusAmpPciState),
        .class_init = vmsg_bus_amp_pci_class_init,
    },
};

static void vmsg_pci_register_types(void)
{
    type_register_static_array(vmsg_pci_info, ARRAY_SIZE(vmsg_pci_info));
}

type_init(vmsg_pci_register_types);
