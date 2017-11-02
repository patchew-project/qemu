/*
 * MIOe-3680 PCI CAN device (SJA1000 based) emulation
 *
 * Copyright (c) 2016 Deniz Eren (deniz.eren@icloud.com)
 *
 * Based on Kvaser PCI CAN device (SJA1000 based) emulation implemented by
 * Jin Yang and Pavel Pisa
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

#include "qemu/osdep.h"
#include "qemu/event_notifier.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "chardev/char.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "can/can_emu.h"

#include "can_sja1000.h"

#define TYPE_CAN_PCI_DEV "mioe3680_pci"

#define MIOe3680_PCI_DEV(obj) \
    OBJECT_CHECK(Mioe3680PCIState, (obj), TYPE_CAN_PCI_DEV)

#ifndef MIOe3680_PCI_VENDOR_ID1
#define MIOe3680_PCI_VENDOR_ID1     0x13fe    /* the PCI device and vendor IDs */
#endif

#ifndef MIOe3680_PCI_DEVICE_ID1
#define MIOe3680_PCI_DEVICE_ID1     0xc302
#endif

#define MIOe3680_PCI_SJA_RANGE     0x800

#define MIOe3680_PCI_BYTES_PER_SJA 0x80

typedef struct Mioe3680PCIState {
    /*< private >*/
    PCIDevice       dev;
    /*< public >*/
    MemoryRegion    sja_io[2];

    CanSJA1000State sja_state[2];
    qemu_irq        irq;

    char            *model; /* The model that support, only SJA1000 now. */
    char            *canbus[2];
    char            *host[2];
} Mioe3680PCIState;

static void mioe3680_pci_irq_raise(void *opaque)
{
    Mioe3680PCIState *d = (Mioe3680PCIState *)opaque;

    qemu_irq_raise(d->irq);
}

static void mioe3680_pci_irq_lower(void *opaque)
{
    Mioe3680PCIState *d = (Mioe3680PCIState *)opaque;

    qemu_irq_lower(d->irq);
}

static void
mioe3680_pci_reset(void *opaque)
{
    Mioe3680PCIState *d = (Mioe3680PCIState *)opaque;
    CanSJA1000State *s1 = &d->sja_state[0];
    CanSJA1000State *s2 = &d->sja_state[1];

    can_sja_hardware_reset(s1);
    can_sja_hardware_reset(s2);
}

static uint64_t mioe3680_pci_sja1_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    Mioe3680PCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[0];

    if (addr >= MIOe3680_PCI_BYTES_PER_SJA) {
        return 0;
    }

    return can_sja_mem_read(s, addr >> 2, size);
}

static void mioe3680_pci_sja1_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    Mioe3680PCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[0];

    if (addr >= MIOe3680_PCI_BYTES_PER_SJA) {
        return;
    }

    can_sja_mem_write(s, addr >> 2, data, size);
}

static uint64_t mioe3680_pci_sja2_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    Mioe3680PCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[1];

    if (addr >= MIOe3680_PCI_BYTES_PER_SJA) {
        return 0;
    }

    return can_sja_mem_read(s, addr >> 2, size);
}

static void mioe3680_pci_sja2_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    Mioe3680PCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[1];

    if (addr >= MIOe3680_PCI_BYTES_PER_SJA) {
        return;
    }

    can_sja_mem_write(s, addr >> 2, data, size);
}

static const MemoryRegionOps mioe3680_pci_sja1_io_ops = {
    .read = mioe3680_pci_sja1_io_read,
    .write = mioe3680_pci_sja1_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps mioe3680_pci_sja2_io_ops = {
    .read = mioe3680_pci_sja2_io_read,
    .write = mioe3680_pci_sja2_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int mioe3680_pci_init(PCIDevice *pci_dev)
{
    Mioe3680PCIState *d = MIOe3680_PCI_DEV(pci_dev);
    CanSJA1000State *s1 = &d->sja_state[0];
    CanSJA1000State *s2 = &d->sja_state[1];
    uint8_t *pci_conf;
    CanBusState *can_bus1;
    CanBusState *can_bus2;

    if (d->model) {
        if (strncmp(d->model, "pcican-s", 256)) { /* for security reason */
            error_report("Can't create CAN device, "
                         "the model %s is not supported now.", d->model);
            exit(1);
        }
    }

    can_bus1 = can_bus_find_by_name(d->canbus[0], true);
    if (can_bus1 == NULL) {
        error_report("Cannot create can find/allocate CAN bus #1");
        exit(1);
    }

    can_bus2 = can_bus_find_by_name(d->canbus[1], true);
    if (can_bus2 == NULL) {
        error_report("Cannot create can find/allocate CAN bus #2");
        exit(1);
    }

    if (d->host[0] != NULL) {
        if (can_bus_connect_to_host_device(can_bus1, d->host[0]) < 0) {
            error_report("Cannot connect CAN bus to host #1 device \"%s\"",
                         d->host[0]);
            exit(1);
        }
    }

    if (d->host[1] != NULL) {
        if (can_bus_connect_to_host_device(can_bus2, d->host[1]) < 0) {
            error_report("Cannot connect CAN bus to host #2 device \"%s\"",
                         d->host[1]);
            exit(1);
        }
    }

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    d->irq = pci_allocate_irq(&d->dev);

    can_sja_init(s1, mioe3680_pci_irq_raise, mioe3680_pci_irq_lower, d);
    can_sja_init(s2, mioe3680_pci_irq_raise, mioe3680_pci_irq_lower, d);

    qemu_register_reset(mioe3680_pci_reset, d);

    if (can_sja_connect_to_bus(s1, can_bus1) < 0) {
        error_report("can_sja_connect_to_bus failed");
        exit(1);
    }

    if (can_sja_connect_to_bus(s2, can_bus2) < 0) {
        error_report("can_sja_connect_to_bus failed");
        exit(1);
    }

    memory_region_init_io(&d->sja_io[0], OBJECT(d), &mioe3680_pci_sja1_io_ops,
                          d, "mioe3680_pci-sja1", MIOe3680_PCI_SJA_RANGE / 2);
    memory_region_init_io(&d->sja_io[1], OBJECT(d), &mioe3680_pci_sja2_io_ops,
                          d, "mioe3680_pci-sja2", MIOe3680_PCI_SJA_RANGE / 2);

    pci_register_bar(&d->dev, /*BAR*/ 0, PCI_BASE_ADDRESS_SPACE_IO,
                                            &d->sja_io[0]);
    pci_register_bar(&d->dev, /*BAR*/ 1, PCI_BASE_ADDRESS_SPACE_IO,
                                            &d->sja_io[1]);

    return 0;
}

static void mioe3680_pci_exit(PCIDevice *pci_dev)
{
    Mioe3680PCIState *d = MIOe3680_PCI_DEV(pci_dev);
    CanSJA1000State *s1 = &d->sja_state[0];
    CanSJA1000State *s2 = &d->sja_state[1];

    can_sja_disconnect(s1);
    can_sja_disconnect(s2);

    qemu_unregister_reset(mioe3680_pci_reset, d);

    /*
     * region d->sja_io is destroyed by QOM now
     */
    /* memory_region_destroy(&d->sja_io[0]); */
    /* memory_region_destroy(&d->sja_io[1]); */

    can_sja_exit(s1);
    can_sja_exit(s2);

    qemu_free_irq(d->irq);
}

static const VMStateDescription vmstate_mioe3680_pci = {
    .name = "mioe3680_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, Mioe3680PCIState),
        VMSTATE_STRUCT(sja_state[0], Mioe3680PCIState, 0, vmstate_can_sja,
                       CanSJA1000State),
        VMSTATE_STRUCT(sja_state[1], Mioe3680PCIState, 0, vmstate_can_sja,
                       CanSJA1000State),
        VMSTATE_END_OF_LIST()
    }
};

static void qdev_mioe3680_pci_reset(DeviceState *dev)
{
    Mioe3680PCIState *d = MIOe3680_PCI_DEV(dev);
    mioe3680_pci_reset(d);
}

static Property mioe3680_pci_properties[] = {
    DEFINE_PROP_STRING("canbus1",   Mioe3680PCIState, canbus[0]),
    DEFINE_PROP_STRING("canbus2",   Mioe3680PCIState, canbus[1]),
    DEFINE_PROP_STRING("host1",  Mioe3680PCIState, host[0]),
    DEFINE_PROP_STRING("host2",  Mioe3680PCIState, host[1]),
    DEFINE_PROP_STRING("model", Mioe3680PCIState, model),
    DEFINE_PROP_END_OF_LIST(),
};

static void mioe3680_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = mioe3680_pci_init;
    k->exit = mioe3680_pci_exit;
    k->vendor_id = MIOe3680_PCI_VENDOR_ID1;
    k->device_id = MIOe3680_PCI_DEVICE_ID1;
    k->revision = 0x00;
    k->class_id = 0x000c09;
    k->subsystem_vendor_id = MIOe3680_PCI_VENDOR_ID1;
    k->subsystem_id = MIOe3680_PCI_DEVICE_ID1;
    dc->desc = "Mioe3680 PCICANx";
    dc->props = mioe3680_pci_properties;
    dc->vmsd = &vmstate_mioe3680_pci;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = qdev_mioe3680_pci_reset;
}

static const TypeInfo mioe3680_pci_info = {
    .name          = TYPE_CAN_PCI_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(Mioe3680PCIState),
    .class_init    = mioe3680_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void mioe3680_pci_register_types(void)
{
    type_register_static(&mioe3680_pci_info);
}

type_init(mioe3680_pci_register_types)
