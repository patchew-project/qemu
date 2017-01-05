/*
 * PCI CAN device (SJA1000 based) emulation
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014 Pavel Pisa
 *
 * Partially based on educational PCIexpress APOHW hardware
 * emulator used fro class A0B36APO at CTU FEE course by
 *    Rostislav Lisovy and Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
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
#include "sysemu/char.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "can/can_emu.h"

#include "can_sja1000.h"

#define TYPE_CAN_PCI_DEV "can_pci"

#define CAN_PCI_DEV(obj) \
    OBJECT_CHECK(CanPCIState, (obj), TYPE_CAN_PCI_DEV)

#define PCI_VENDOR_ID_CAN_PCI      PCI_VENDOR_ID_REDHAT
#define PCI_DEVICE_ID_CAN_PCI      0xbeef
#define PCI_REVISION_ID_CAN_PCI    0x73

typedef struct CanPCIState {
    /*< private >*/
    PCIDevice       dev;
    /*< public >*/
    MemoryRegion    sja_mmio;
    CanSJA1000State sja_state;
    qemu_irq        irq;


    char            *model; /* The model that support, only SJA1000 now. */
    char            *canbus;
    char            *host;
} CanPCIState;

static void can_pci_irq_raise(void *opaque)
{
    CanPCIState *d = (CanPCIState *)opaque;
    qemu_irq_raise(d->irq);
}

static void can_pci_irq_lower(void *opaque)
{
    CanPCIState *d = (CanPCIState *)opaque;
    qemu_irq_lower(d->irq);
}

static void
can_pci_reset(void *opaque)
{
    CanPCIState *d = (CanPCIState *)opaque;
    CanSJA1000State *s = &d->sja_state;

    can_sja_hardware_reset(s);
}

static uint64_t can_pci_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    CanPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state;

    return can_sja_mem_read(s, addr, size);
}

static void can_pci_bar0_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    CanPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state;

    can_sja_mem_write(s, addr, data, size);
}

static const MemoryRegionOps can_pci_bar0_ops = {
    .read = can_pci_bar0_read,
    .write = can_pci_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int can_pci_init(PCIDevice *pci_dev)
{
    CanPCIState *d = CAN_PCI_DEV(pci_dev);
    CanSJA1000State *s = &d->sja_state;
    uint8_t *pci_conf;
    CanBusState *can_bus;

    if (d->model) {
        if (strncmp(d->model, "SJA1000", 256)) { /* for security reason */
            error_report("Can't create CAN device, "
                         "the model %s is not supported now.\n", d->model);
            exit(1);
        }
    }

    can_bus = can_bus_find_by_name(d->canbus, true);
    if (can_bus == NULL) {
        error_report("Cannot create can find/allocate CAN bus\n");
        exit(1);

    }

    if (d->host != NULL) {
        if (can_bus_connect_to_host_device(can_bus, d->host) < 0) {
            error_report("Cannot connect CAN bus to host device \"%s\"\n", d->host);
            exit(1);
        }
    }

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    d->irq = pci_allocate_irq(&d->dev);

    can_sja_init(s, can_pci_irq_raise, can_pci_irq_lower, d);

    qemu_register_reset(can_pci_reset, d);

    if (can_sja_connect_to_bus(s, can_bus) < 0) {
        error_report("can_sja_connect_to_bus failed\n");
        exit(1);
    }

    memory_region_init_io(&d->sja_mmio, OBJECT(d), &can_pci_bar0_ops, d,
                          "can_pci-bar0", CAN_SJA_MEM_SIZE);

    pci_register_bar(pci_dev, /*BAR*/ 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->sja_mmio);

    return 0;
}

static void can_pci_exit(PCIDevice *pci_dev)
{
    CanPCIState *d = CAN_PCI_DEV(pci_dev);
    CanSJA1000State *s = &d->sja_state;

    can_sja_disconnect(s);

    qemu_unregister_reset(can_pci_reset, d);

    /* region d->sja_mmio is destroyed by QOM now */
    /* memory_region_destroy(&d->sja_mmio); */

    can_sja_exit(s);

    qemu_free_irq(d->irq);
}

static const VMStateDescription vmstate_can_pci = {
    .name = "can_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, CanPCIState),
        VMSTATE_STRUCT(sja_state, CanPCIState, 0, vmstate_can_sja, CanSJA1000State),
        /*char *model,*/
        VMSTATE_END_OF_LIST()
    }
};

static void qdev_can_pci_reset(DeviceState *dev)
{
    CanPCIState *d = CAN_PCI_DEV(dev);
    can_pci_reset(d);
}

static Property can_pci_properties[] = {
    DEFINE_PROP_STRING("canbus",   CanPCIState, canbus),
    DEFINE_PROP_STRING("host",  CanPCIState, host),
    DEFINE_PROP_STRING("model", CanPCIState, model),
    DEFINE_PROP_END_OF_LIST(),
};

static void can_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = can_pci_init;
    k->exit = can_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_CAN_PCI;
    k->device_id = PCI_DEVICE_ID_CAN_PCI;
    k->revision = PCI_REVISION_ID_CAN_PCI;
    k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
    dc->desc = "CAN PCI SJA1000";
    dc->props = can_pci_properties;
    dc->vmsd = &vmstate_can_pci;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = qdev_can_pci_reset;
}

static const TypeInfo can_pci_info = {
    .name          = TYPE_CAN_PCI_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CanPCIState),
    .class_init    = can_pci_class_init,
};

static void can_pci_register_types(void)
{
    type_register_static(&can_pci_info);
}

type_init(can_pci_register_types)
