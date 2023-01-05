/*
 * QEMU PIIX4 PCI Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2018 Hervé Poussineau
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
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/southbridge/piix.h"
#include "hw/pci/pci.h"
#include "hw/ide/piix.h"
#include "hw/isa/isa.h"
#include "hw/intc/i8259.h"
#include "hw/dma/i8257.h"
#include "hw/timer/i8254.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/ide/pci.h"
#include "hw/acpi/piix4.h"
#include "hw/usb/hcd-uhci.h"
#include "migration/vmstate.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "qom/object.h"

static void piix4_set_irq(void *opaque, int irq_num, int level)
{
    int i, pic_irq, pic_level;
    PIIXState *s = opaque;
    PCIBus *bus = pci_get_bus(&s->dev);

    /* now we change the pic irq level according to the piix irq mappings */
    /* XXX: optimize */
    pic_irq = s->dev.config[PIIX_PIRQCA + irq_num];
    if (pic_irq < ISA_NUM_IRQS) {
        /* The pic level is the logical OR of all the PCI irqs mapped to it. */
        pic_level = 0;
        for (i = 0; i < PIIX_NUM_PIRQS; i++) {
            if (pic_irq == s->dev.config[PIIX_PIRQCA + i]) {
                pic_level |= pci_bus_get_irq_level(bus, i);
            }
        }
        qemu_set_irq(s->pic.in_irqs[pic_irq], pic_level);
    }
}

static void piix4_isa_reset(DeviceState *dev)
{
    PIIXState *d = PIIX_PCI_DEVICE(dev);
    uint8_t *pci_conf = d->dev.config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x80;
    pci_conf[0x61] = 0x80;
    pci_conf[0x62] = 0x80;
    pci_conf[0x63] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;

    d->pic_levels = 0; /* not used in PIIX4 */
    d->rcr = 0;
}

static int piix4_post_load(void *opaque, int version_id)
{
    PIIXState *s = opaque;

    if (version_id == 2) {
        s->rcr = 0;
    }

    return 0;
}

static const VMStateDescription vmstate_piix4 = {
    .name = "PIIX4",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = piix4_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIXState),
        VMSTATE_UINT8_V(rcr, PIIXState, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void piix4_rcr_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned int len)
{
    PIIXState *s = opaque;

    if (val & 4) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }

    s->rcr = val & 2; /* keep System Reset type only */
}

static uint64_t piix4_rcr_read(void *opaque, hwaddr addr, unsigned int len)
{
    PIIXState *s = opaque;

    return s->rcr;
}

static const MemoryRegionOps piix4_rcr_ops = {
    .read = piix4_rcr_read,
    .write = piix4_rcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void piix4_realize(PCIDevice *dev, Error **errp)
{
    PIIXState *s = PIIX_PCI_DEVICE(dev);
    PCIBus *pci_bus = pci_get_bus(dev);
    ISABus *isa_bus;

    isa_bus = isa_bus_new(DEVICE(dev), pci_address_space(dev),
                          pci_address_space_io(dev), errp);
    if (!isa_bus) {
        return;
    }

    memory_region_init_io(&s->rcr_mem, OBJECT(dev), &piix4_rcr_ops, s,
                          "reset-control", 1);
    memory_region_add_subregion_overlap(pci_address_space_io(dev),
                                        PIIX_RCR_IOPORT, &s->rcr_mem, 1);

    /* initialize i8259 pic */
    if (!qdev_realize(DEVICE(&s->pic), NULL, errp)) {
        return;
    }

    /* initialize ISA irqs */
    isa_bus_irqs(isa_bus, s->pic.in_irqs);

    /* initialize pit */
    i8254_pit_init(isa_bus, 0x40, 0, NULL);

    /* DMA */
    i8257_dma_init(isa_bus, 0);

    /* RTC */
    qdev_prop_set_int32(DEVICE(&s->rtc), "base_year", 2000);
    if (!qdev_realize(DEVICE(&s->rtc), BUS(isa_bus), errp)) {
        return;
    }
    s->rtc.irq = qdev_get_gpio_in(DEVICE(&s->pic), s->rtc.isairq);

    /* IDE */
    qdev_prop_set_int32(DEVICE(&s->ide), "addr", dev->devfn + 1);
    if (!qdev_realize(DEVICE(&s->ide), BUS(pci_bus), errp)) {
        return;
    }

    /* USB */
    if (s->has_usb) {
        object_initialize_child(OBJECT(dev), "uhci", &s->uhci,
                                TYPE_PIIX4_USB_UHCI);
        qdev_prop_set_int32(DEVICE(&s->uhci), "addr", dev->devfn + 2);
        if (!qdev_realize(DEVICE(&s->uhci), BUS(pci_bus), errp)) {
            return;
        }
    }

    /* ACPI controller */
    if (s->has_acpi) {
        object_initialize_child(OBJECT(s), "pm", &s->pm, TYPE_PIIX4_PM);
        qdev_prop_set_int32(DEVICE(&s->pm), "addr", dev->devfn + 3);
        qdev_prop_set_uint32(DEVICE(&s->pm), "smb_io_base", s->smb_io_base);
        qdev_prop_set_bit(DEVICE(&s->pm), "smm-enabled", s->smm_enabled);
        if (!qdev_realize(DEVICE(&s->pm), BUS(pci_bus), errp)) {
            return;
        }
        qdev_connect_gpio_out(DEVICE(&s->pm), 0,
                              qdev_get_gpio_in(DEVICE(&s->pic), 9));
    }

    pci_bus_irqs(pci_bus, piix4_set_irq, s, PIIX_NUM_PIRQS);
}

static void piix4_init(Object *obj)
{
    PIIXState *s = PIIX_PCI_DEVICE(obj);

    object_initialize_child(obj, "pic", &s->pic, TYPE_ISA_PIC);
    object_initialize_child(obj, "rtc", &s->rtc, TYPE_MC146818_RTC);
    object_initialize_child(obj, "ide", &s->ide, TYPE_PIIX4_IDE);
}

static Property piix4_props[] = {
    DEFINE_PROP_UINT32("smb_io_base", PIIXState, smb_io_base, 0),
    DEFINE_PROP_BOOL("has-acpi", PIIXState, has_acpi, true),
    DEFINE_PROP_BOOL("has-usb", PIIXState, has_usb, true),
    DEFINE_PROP_BOOL("smm-enabled", PIIXState, smm_enabled, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void piix4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = piix4_realize;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_0;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    dc->reset = piix4_isa_reset;
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_piix4;
    /*
     * Reason: part of PIIX4 southbridge, needs to be wired up,
     * e.g. by mips_malta_init()
     */
    dc->user_creatable = false;
    dc->hotpluggable = false;
    device_class_set_props(dc, piix4_props);
}

static const TypeInfo piix4_info = {
    .name          = TYPE_PIIX4_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIXState),
    .instance_init = piix4_init,
    .class_init    = piix4_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void piix4_register_types(void)
{
    type_register_static(&piix4_info);
}

type_init(piix4_register_types)
