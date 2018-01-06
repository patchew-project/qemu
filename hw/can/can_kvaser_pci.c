/*
 * Kvaser PCI CAN device (SJA1000 based) emulation
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
#include "chardev/char.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "can/can_emu.h"

#include "can_sja1000.h"

#define TYPE_CAN_PCI_DEV "kvaser_pci"

#define KVASER_PCI_DEV(obj) \
    OBJECT_CHECK(KvaserPCIState, (obj), TYPE_CAN_PCI_DEV)

#ifndef KVASER_PCI_VENDOR_ID1
#define KVASER_PCI_VENDOR_ID1     0x10e8    /* the PCI device and vendor IDs */
#endif

#ifndef KVASER_PCI_DEVICE_ID1
#define KVASER_PCI_DEVICE_ID1     0x8406
#endif

#define KVASER_PCI_S5920_RANGE    0x80
#define KVASER_PCI_SJA_RANGE      0x80
#define KVASER_PCI_XILINX_RANGE   0x8

#define KVASER_PCI_BYTES_PER_SJA  0x20

#define S5920_OMB                 0x0C
#define S5920_IMB                 0x1C
#define S5920_MBEF                0x34
#define S5920_INTCSR              0x38
#define S5920_RCR                 0x3C
#define S5920_PTCR                0x60

#define S5920_INTCSR_ADDON_INTENABLE_M        0x2000
#define S5920_INTCSR_INTERRUPT_ASSERTED_M     0x800000

#define KVASER_PCI_XILINX_VERINT  7   /* Lower nibble simulate interrupts,
                                         high nibble version number. */

#define KVASER_PCI_XILINX_VERSION_NUMBER 13

typedef struct KvaserPCIState {
    /*< private >*/
    PCIDevice       dev;
    /*< public >*/
    MemoryRegion    s5920_io;
    MemoryRegion    sja_io;
    MemoryRegion    xilinx_io;

    CanSJA1000State sja_state;
    qemu_irq        irq;

    uint32_t        s5920_intcsr;
    uint32_t        s5920_irqstate;

    char            *model; /* The model that support, only SJA1000 now. */
    char            *canbus;
    char            *host;
} KvaserPCIState;

static void kvaser_pci_irq_raise(void *opaque)
{
    KvaserPCIState *d = (KvaserPCIState *)opaque;
    d->s5920_irqstate = 1;

    if (d->s5920_intcsr & S5920_INTCSR_ADDON_INTENABLE_M) {
        qemu_irq_raise(d->irq);
    }
}

static void kvaser_pci_irq_lower(void *opaque)
{
    KvaserPCIState *d = (KvaserPCIState *)opaque;
    d->s5920_irqstate = 0;
    qemu_irq_lower(d->irq);
}

static void
kvaser_pci_reset(void *opaque)
{
    KvaserPCIState *d = (KvaserPCIState *)opaque;
    CanSJA1000State *s = &d->sja_state;

    can_sja_hardware_reset(s);
}

static uint64_t kvaser_pci_s5920_io_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    KvaserPCIState *d = opaque;
    uint64_t val;

    switch (addr) {
    case S5920_INTCSR:
        val = d->s5920_intcsr;
        val &= ~S5920_INTCSR_INTERRUPT_ASSERTED_M;
        if (d->s5920_irqstate) {
            val |= S5920_INTCSR_INTERRUPT_ASSERTED_M;
        }
        return val;
    }
    return 0;
}

static void kvaser_pci_s5920_io_write(void *opaque, hwaddr addr, uint64_t data,
                                      unsigned size)
{
    KvaserPCIState *d = opaque;

    switch (addr) {
    case S5920_INTCSR:
        if (~d->s5920_intcsr & data & S5920_INTCSR_ADDON_INTENABLE_M) {
            if (d->s5920_irqstate) {
                qemu_irq_raise(d->irq);
            }
        }
        d->s5920_intcsr = data;
        break;
    }
}

static uint64_t kvaser_pci_sja_io_read(void *opaque, hwaddr addr, unsigned size)
{
    KvaserPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state;

    if (addr >= KVASER_PCI_BYTES_PER_SJA) {
        return 0;
    }

    return can_sja_mem_read(s, addr, size);
}

static void kvaser_pci_sja_io_write(void *opaque, hwaddr addr, uint64_t data,
                                    unsigned size)
{
    KvaserPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state;

    if (addr >= KVASER_PCI_BYTES_PER_SJA) {
        return;
    }

    can_sja_mem_write(s, addr, data, size);
}

static uint64_t kvaser_pci_xilinx_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    /*KvaserPCIState *d = opaque;*/

    switch (addr) {
    case KVASER_PCI_XILINX_VERINT:
        return (KVASER_PCI_XILINX_VERSION_NUMBER << 4) | 0;
    }

    return 0;
}

static void kvaser_pci_xilinx_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    /*KvaserPCIState *d = opaque;*/
}

static const MemoryRegionOps kvaser_pci_s5920_io_ops = {
    .read = kvaser_pci_s5920_io_read,
    .write = kvaser_pci_s5920_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps kvaser_pci_sja_io_ops = {
    .read = kvaser_pci_sja_io_read,
    .write = kvaser_pci_sja_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps kvaser_pci_xilinx_io_ops = {
    .read = kvaser_pci_xilinx_io_read,
    .write = kvaser_pci_xilinx_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int kvaser_pci_init(PCIDevice *pci_dev)
{
    KvaserPCIState *d = KVASER_PCI_DEV(pci_dev);
    CanSJA1000State *s = &d->sja_state;
    uint8_t *pci_conf;
    CanBusState *can_bus;

    if (d->model) {
        if (strncmp(d->model, "pcican-s", 256)) { /* for security reason */
            error_report("Can't create CAN device, "
                         "the model %s is not supported now.", d->model);
            exit(1);
        }
    }

    can_bus = can_bus_find_by_name(d->canbus, true);
    if (can_bus == NULL) {
        error_report("Cannot create can find/allocate CAN bus");
        exit(1);

    }

    if (d->host != NULL) {
        if (can_bus_connect_to_host_device(can_bus, d->host) < 0) {
            error_report("Cannot connect CAN bus to host device \"%s\"",
                         d->host);
            exit(1);
        }
    }

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    d->irq = pci_allocate_irq(&d->dev);

    can_sja_init(s, kvaser_pci_irq_raise, kvaser_pci_irq_lower, d);

    qemu_register_reset(kvaser_pci_reset, d);

    if (can_sja_connect_to_bus(s, can_bus) < 0) {
        error_report("can_sja_connect_to_bus failed");
        exit(1);
    }

    memory_region_init_io(&d->s5920_io, OBJECT(d), &kvaser_pci_s5920_io_ops,
                          d, "kvaser_pci-s5920", KVASER_PCI_S5920_RANGE);
    memory_region_init_io(&d->sja_io, OBJECT(d), &kvaser_pci_sja_io_ops,
                          d, "kvaser_pci-sja", KVASER_PCI_SJA_RANGE);
    memory_region_init_io(&d->xilinx_io, OBJECT(d), &kvaser_pci_xilinx_io_ops,
                          d, "kvaser_pci-xilinx", KVASER_PCI_XILINX_RANGE);

    pci_register_bar(&d->dev, /*BAR*/ 0, PCI_BASE_ADDRESS_SPACE_IO,
                                            &d->s5920_io);
    pci_register_bar(&d->dev, /*BAR*/ 1, PCI_BASE_ADDRESS_SPACE_IO,
                                            &d->sja_io);
    pci_register_bar(&d->dev, /*BAR*/ 2, PCI_BASE_ADDRESS_SPACE_IO,
                                            &d->xilinx_io);

    return 0;
}

static void kvaser_pci_exit(PCIDevice *pci_dev)
{
    KvaserPCIState *d = KVASER_PCI_DEV(pci_dev);
    CanSJA1000State *s = &d->sja_state;

    can_sja_disconnect(s);

    qemu_unregister_reset(kvaser_pci_reset, d);

    /*
     * regions d->s5920_io, d->sja_io and d->xilinx_io
     * are destroyed by QOM now
     */

    can_sja_exit(s);

    qemu_free_irq(d->irq);
}

static const VMStateDescription vmstate_kvaser_pci = {
    .name = "kvaser_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, KvaserPCIState),
        VMSTATE_STRUCT(sja_state, KvaserPCIState, 0, vmstate_can_sja,
                       CanSJA1000State),
        /*char *model,*/
        VMSTATE_UINT32(s5920_intcsr, KvaserPCIState),
        VMSTATE_UINT32(s5920_irqstate, KvaserPCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void qdev_kvaser_pci_reset(DeviceState *dev)
{
    KvaserPCIState *d = KVASER_PCI_DEV(dev);
    kvaser_pci_reset(d);
}

static Property kvaser_pci_properties[] = {
    DEFINE_PROP_STRING("canbus",   KvaserPCIState, canbus),
    DEFINE_PROP_STRING("host",  KvaserPCIState, host),
    DEFINE_PROP_STRING("model", KvaserPCIState, model),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvaser_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = kvaser_pci_init;
    k->exit = kvaser_pci_exit;
    k->vendor_id = KVASER_PCI_VENDOR_ID1;
    k->device_id = KVASER_PCI_DEVICE_ID1;
    k->revision = 0x00;
    k->class_id = 0x00ff00;
    dc->desc = "Kvaser PCICANx";
    dc->props = kvaser_pci_properties;
    dc->vmsd = &vmstate_kvaser_pci;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = qdev_kvaser_pci_reset;
}

static const TypeInfo kvaser_pci_info = {
    .name          = TYPE_CAN_PCI_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(KvaserPCIState),
    .class_init    = kvaser_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void kvaser_pci_register_types(void)
{
    type_register_static(&kvaser_pci_info);
}

type_init(kvaser_pci_register_types)
