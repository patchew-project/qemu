/*
 * PCM-26D2CA PCIe CAN device (SJA1000 based) emulation
 *
 * Advantech iDoor Module: 2-Ports Isolated CANBus mPCIe, DB9
 *
 * Copyright (c) 2023 Deniz Eren (deniz.eren@icloud.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/units.h"
#include "qemu/osdep.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

#include "can_sja1000.h"
#include "qom/object.h"

#define TYPE_CAN_PCI_DEV "pcm26d2ca_pci"

typedef struct Pcm26D2CAPCIeState Pcm26D2CAPCIeState;
DECLARE_INSTANCE_CHECKER(Pcm26D2CAPCIeState, PCM26D2CA_PCI_DEV,
                         TYPE_CAN_PCI_DEV)

/* the PCI device and vendor IDs */
#ifndef PCM26D2CA_PCI_VENDOR_ID1
#define PCM26D2CA_PCI_VENDOR_ID1    0x13fe
#endif

#ifndef PCM26D2CA_PCI_DEVICE_ID1
#define PCM26D2CA_PCI_DEVICE_ID1    0x00d7
#endif

#define PCM26D2CA_PCI_SJA_COUNT     2
#define PCM26D2CA_PCI_SJA_RANGE     0x400

#define PCM26D2CA_PCI_BYTES_PER_SJA 0x80

#define PCM26D2CA_IO_IDX            0

#define PCM26D2CA_MSI_VEC_NUM       (8)
#define PCM26D2CA_MSI_RI_ENTRY      (0) /* Receive interrupt */
#define PCM26D2CA_MSI_TI_ENTRY      (1) /* Transmit interrupt */
#define PCM26D2CA_MSI_EI_ENTRY      (2) /* Error warning interrupt */
#define PCM26D2CA_MSI_DOI_ENTRY     (3) /* Data overrun interrupt */
#define PCM26D2CA_MSI_WUI_ENTRY     (4) /* Wakeup interrupt */
#define PCM26D2CA_MSI_EPI_ENTRY     (5) /* Error passive */
#define PCM26D2CA_MSI_ALI_ENTRY     (6) /* Arbitration lost */
#define PCM26D2CA_MSI_BEI_ENTRY     (7) /* Bus error interrupt */

struct Pcm26D2CAPCIeState {
    /*< private >*/
    PCIDevice       dev;
    /*< public >*/
    MemoryRegion    io;

    CanSJA1000State sja_state[PCM26D2CA_PCI_SJA_COUNT];
    qemu_irq        irq;

    char            *model; /* The model that support, only SJA1000 now. */
    CanBusState     *canbus[PCM26D2CA_PCI_SJA_COUNT];
};

static void pcm26d2ca_pci_reset(DeviceState *dev)
{
    Pcm26D2CAPCIeState *d = PCM26D2CA_PCI_DEV(dev);
    int i;

    for (i = 0 ; i < PCM26D2CA_PCI_SJA_COUNT; i++) {
        can_sja_hardware_reset(&d->sja_state[i]);
    }
}

static uint64_t pcm26d2ca_pci_io_read(void *opaque, hwaddr addr, unsigned size)
{
    Pcm26D2CAPCIeState *d = opaque;
    CanSJA1000State *s = &d->sja_state[0];
    hwaddr _addr = addr;

    if (addr >= PCM26D2CA_PCI_SJA_RANGE) {
        s = &d->sja_state[1];
        _addr -= PCM26D2CA_PCI_SJA_RANGE;
    }

    if (_addr >= PCM26D2CA_PCI_BYTES_PER_SJA) {
        return 0;
    }

    return can_sja_mem_read(s, _addr >> 2, size);
}

static void pcm26d2ca_pci_io_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned size)
{
    Pcm26D2CAPCIeState *d = opaque;
    CanSJA1000State *s = &d->sja_state[0];
    hwaddr _addr = addr;

    if (addr >= PCM26D2CA_PCI_SJA_RANGE) {
        s = &d->sja_state[1];
        _addr -= PCM26D2CA_PCI_SJA_RANGE;
    }

    if (_addr >= PCM26D2CA_PCI_BYTES_PER_SJA) {
        return;
    }

    can_sja_mem_write(s, _addr >> 2, data, size);
}

static const MemoryRegionOps pcm26d2ca_pci_io_ops = {
    .read = pcm26d2ca_pci_io_read,
    .write = pcm26d2ca_pci_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .max_access_size = 1,
    },
};

static void pcm26d2ca_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    static const uint16_t pcie_offset = 0x0E0;
    Pcm26D2CAPCIeState *d = PCM26D2CA_PCI_DEV(pci_dev);
    uint8_t *pci_conf;
    Error *err = NULL;
    int i;
    int ret;

    /* Map MSI and MSI-X vector entries one-to-one for each interrupt */
    uint8_t msi_map[PCM26D2CA_MSI_VEC_NUM] = {
        PCM26D2CA_MSI_RI_ENTRY,  /* Receive interrupt */
        PCM26D2CA_MSI_TI_ENTRY,  /* Transmit interrupt */
        PCM26D2CA_MSI_EI_ENTRY,  /* Error warning interrupt */
        PCM26D2CA_MSI_DOI_ENTRY, /* Data overrun interrupt */
        PCM26D2CA_MSI_WUI_ENTRY, /* Wakeup interrupt */
        PCM26D2CA_MSI_EPI_ENTRY, /* Error passive */
        PCM26D2CA_MSI_ALI_ENTRY, /* Arbitration lost */
        PCM26D2CA_MSI_BEI_ENTRY  /* Bus error interrupt */
    };

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    d->irq = pci_allocate_irq(&d->dev);

    for (i = 0 ; i < PCM26D2CA_PCI_SJA_COUNT; i++) {
        can_sja_cap_init(&d->sja_state[i], d->irq, pci_dev, msi_map, msi_map);
    }

    for (i = 0 ; i < PCM26D2CA_PCI_SJA_COUNT; i++) {
        if (can_sja_connect_to_bus(&d->sja_state[i], d->canbus[i]) < 0) {
            error_setg(errp, "can_sja_connect_to_bus failed");
            return;
        }
    }

    memory_region_init_io(&d->io, OBJECT(d), &pcm26d2ca_pci_io_ops,
                          d, "pcm26d2ca_pci-io", 2*PCM26D2CA_PCI_SJA_RANGE);
    pci_register_bar(&d->dev, PCM26D2CA_IO_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &d->io);

    if (pcie_endpoint_cap_v1_init(pci_dev, pcie_offset) < 0) {
        error_setg(errp, "Failed to initialize PCIe capability");
        return;
    }

    ret = msi_init( PCI_DEVICE(d), 0xD0, PCM26D2CA_MSI_VEC_NUM,
            true, false, NULL );

    if (ret) {
        error_setg(errp, "msi_init failed (%d)", ret);
        return;
    }

    error_free(err);
}

static void pcm26d2ca_pci_exit(PCIDevice *pci_dev)
{
    Pcm26D2CAPCIeState *d = PCM26D2CA_PCI_DEV(pci_dev);
    int i;

    for (i = 0 ; i < PCM26D2CA_PCI_SJA_COUNT; i++) {
        can_sja_disconnect(&d->sja_state[i]);
    }

    qemu_free_irq(d->irq);
    msi_uninit(pci_dev);
}

static const VMStateDescription vmstate_pcm26d2ca_pci = {
    .name = TYPE_CAN_PCI_DEV,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, Pcm26D2CAPCIeState),
        VMSTATE_STRUCT(sja_state[0], Pcm26D2CAPCIeState, 0, vmstate_can_sja,
                       CanSJA1000State),
        VMSTATE_STRUCT(sja_state[1], Pcm26D2CAPCIeState, 0, vmstate_can_sja,
                       CanSJA1000State),
        VMSTATE_END_OF_LIST()
    }
};

static void pcm26d2ca_pci_instance_init(Object *obj)
{
    Pcm26D2CAPCIeState *d = PCM26D2CA_PCI_DEV(obj);

    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&d->canbus[0],
                             qdev_prop_allow_set_link_before_realize,
                             0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&d->canbus[1],
                             qdev_prop_allow_set_link_before_realize,
                             0);
}

static void pcm26d2ca_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcm26d2ca_pci_realize;
    k->exit = pcm26d2ca_pci_exit;
    k->vendor_id = PCM26D2CA_PCI_VENDOR_ID1;
    k->device_id = PCM26D2CA_PCI_DEVICE_ID1;
    k->revision = 0x00;
    k->class_id = 0x000c09;
    k->subsystem_vendor_id = PCM26D2CA_PCI_VENDOR_ID1;
    k->subsystem_id = PCM26D2CA_PCI_DEVICE_ID1;
    dc->desc = "PCM-26 series Advantech iDoor";
    dc->vmsd = &vmstate_pcm26d2ca_pci;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = pcm26d2ca_pci_reset;
}

static const TypeInfo pcm26d2ca_pci_info = {
    .name          = TYPE_CAN_PCI_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(Pcm26D2CAPCIeState),
    .class_init    = pcm26d2ca_pci_class_init,
    .instance_init = pcm26d2ca_pci_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void pcm26d2ca_pci_register_types(void)
{
    type_register_static(&pcm26d2ca_pci_info);
}

type_init(pcm26d2ca_pci_register_types)
