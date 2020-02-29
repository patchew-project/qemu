/*
 * QEMU VIA southbridge IDE emulation (VT82C686B, VT8231)
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2010 Huacai Chen <zltjiangshi@gmail.com>
 * Copyright (c) 2019-2020 BALATON Zoltan
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
#include "qemu/range.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "sysemu/dma.h"

#include "hw/ide/pci.h"
#include "trace.h"

static uint64_t bmdma_read(void *opaque, hwaddr addr,
                           unsigned size)
{
    BMDMAState *bm = opaque;
    uint32_t val;

    if (size != 1) {
        return ((uint64_t)1 << (size * 8)) - 1;
    }

    switch (addr & 3) {
    case 0:
        val = bm->cmd;
        break;
    case 2:
        val = bm->status;
        break;
    default:
        val = 0xff;
        break;
    }

    trace_bmdma_read_via(addr, val);
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned size)
{
    BMDMAState *bm = opaque;

    if (size != 1) {
        return;
    }

    trace_bmdma_write_via(addr, val);
    switch (addr & 3) {
    case 0:
        bmdma_cmd_writeb(bm, val);
        break;
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    default:;
    }
}

static const MemoryRegionOps via_bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
};

static void bmdma_setup_bar(PCIIDEState *d)
{
    int i;

    memory_region_init(&d->bmdma_bar, OBJECT(d), "via-bmdma-container", 16);
    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        memory_region_init_io(&bm->extra_io, OBJECT(d), &via_bmdma_ops, bm,
                              "via-bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8, &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, OBJECT(d),
                              &bmdma_addr_ioport_ops, bm, "bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8 + 4, &bm->addr_ioport);
    }
}

static void via_ide_set_irq(void *opaque, int n, int level)
{
    PCIDevice *d = PCI_DEVICE(opaque);

    if (level) {
        d->config[0x70 + n * 8] |= 0x80;
    } else {
        d->config[0x70 + n * 8] &= ~0x80;
    }
    level = (d->config[0x70] & 0x80) || (d->config[0x78] & 0x80);

    /*
     * Some machines operate in "non 100% native mode" where PCI_INTERRUPT_LINE
     * is not used but IDE always uses ISA IRQ 14 and 15 even in native mode.
     * Some guest drivers expect this, often without checking.
     */
    if (!(pci_get_byte(d->config + PCI_CLASS_PROG) & (n ? 4 : 1)) ||
        PCI_IDE(d)->flags & BIT(PCI_IDE_LEGACY_IRQ)) {
        qemu_set_irq(isa_get_irq(NULL, (n ? 15 : 14)), level);
    } else {
        n = pci_get_byte(d->config + PCI_INTERRUPT_LINE);
        if (n) {
            qemu_set_irq(isa_get_irq(NULL, n), level);
        }
    }
}

static uint32_t via_ide_config_read(PCIDevice *d, uint32_t address, int len)
{
    /*
     * The pegasos2 firmware writes to PCI_INTERRUPT_LINE but on real
     * hardware it's fixed at 14 and won't change. Some guests also expect
     * legacy interrupts, without reading PCI_INTERRUPT_LINE but Linux
     * depends on this to read 14. We set it to 14 in the reset method and
     * also set the wmask to 0 to emulate this but that turns out to be not
     * enough. QEMU resets the PCI bus after this device and
     * pci_do_device_reset() called from pci_device_reset() will zero
     * PCI_INTERRUPT_LINE so this config_read function is to counter that and
     * restore the correct value, otherwise this should not be needed.
     */
    if (range_covers_byte(address, len, PCI_INTERRUPT_LINE)) {
        pci_set_byte(d->config + PCI_INTERRUPT_LINE, 14);
    }
    return pci_default_read_config(d, address, len);
}

static void via_ide_reset(DeviceState *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    PCIDevice *pd = PCI_DEVICE(dev);
    uint8_t *pci_conf = pd->config;
    int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_WAIT);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_FAST_BACK |
                 PCI_STATUS_DEVSEL_MEDIUM);

    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0x000001f0);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0x000003f4);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0x00000170);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0x00000374);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0x0000cc01); /* BMIBA: 20-23h */
    pci_set_long(pci_conf + PCI_INTERRUPT_LINE, 0x0000010e);

    /* IDE chip enable, IDE configuration 1/2, IDE FIFO Configuration*/
    pci_set_long(pci_conf + 0x40, 0x0a090600);
    /* IDE misc configuration 1/2/3 */
    pci_set_long(pci_conf + 0x44, 0x00c00068);
    /* IDE Timing control */
    pci_set_long(pci_conf + 0x48, 0xa8a8a8a8);
    /* IDE Address Setup Time */
    pci_set_long(pci_conf + 0x4c, 0x000000ff);
    /* UltraDMA Extended Timing Control*/
    pci_set_long(pci_conf + 0x50, 0x07070707);
    /* UltraDMA FIFO Control */
    pci_set_long(pci_conf + 0x54, 0x00000004);
    /* IDE primary sector size */
    pci_set_long(pci_conf + 0x60, 0x00000200);
    /* IDE secondary sector size */
    pci_set_long(pci_conf + 0x68, 0x00000200);
    /* PCI PM Block */
    pci_set_long(pci_conf + 0xc0, 0x00020001);
}

static void via_ide_realize(PCIDevice *dev, Error **errp)
{
    PCIIDEState *d = PCI_IDE(dev);
    uint8_t *pci_conf = dev->config;
    int i;

    pci_config_set_prog_interface(pci_conf, 0x8f); /* native PCI ATA mode */
    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x000000c0);
    dev->wmask[PCI_CLASS_PROG] = 5;
    dev->wmask[PCI_INTERRUPT_LINE] = 0;

    memory_region_init_io(&d->data_bar[0], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[0], "via-ide0-data", 8);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[0]);

    memory_region_init_io(&d->cmd_bar[0], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[0], "via-ide0-cmd", 4);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[0]);

    memory_region_init_io(&d->data_bar[1], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[1], "via-ide1-data", 8);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[1]);

    memory_region_init_io(&d->cmd_bar[1], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[1], "via-ide1-cmd", 4);
    pci_register_bar(dev, 3, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[1]);

    bmdma_setup_bar(d);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);

    vmstate_register(VMSTATE_IF(dev), 0, &vmstate_ide_pci, d);

    for (i = 0; i < 2; i++) {
        ide_bus_new(&d->bus[i], sizeof(d->bus[i]), DEVICE(d), i, 2);
        ide_init2(&d->bus[i], qemu_allocate_irq(via_ide_set_irq, d, i));

        bmdma_init(&d->bus[i], &d->bmdma[i], d);
        d->bmdma[i].bus = &d->bus[i];
        ide_register_restart_cb(&d->bus[i]);
    }
}

static void via_ide_exitfn(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    unsigned i;

    for (i = 0; i < 2; ++i) {
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
    }
}

void via_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn,
                  bool legacy_irq)
{
    PCIDevice *dev;

    dev = pci_create(bus, devfn, "via-ide");
    qdev_prop_set_bit(&dev->qdev, "legacy-irq", legacy_irq);
    qdev_init_nofail(&dev->qdev);
    pci_ide_create_devs(dev, hd_table);
}

static Property via_ide_properties[] = {
    DEFINE_PROP_BIT("legacy-irq", PCIIDEState, flags, PCI_IDE_LEGACY_IRQ,
                    false),
    DEFINE_PROP_END_OF_LIST(),
};

static void via_ide_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->reset = via_ide_reset;
    k->realize = via_ide_realize;
    k->exit = via_ide_exitfn;
    k->config_read = via_ide_config_read;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_IDE;
    k->revision = 0x06;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    device_class_set_props(dc, via_ide_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo via_ide_info = {
    .name          = "via-ide",
    .parent        = TYPE_PCI_IDE,
    .class_init    = via_ide_class_init,
};

static void via_ide_register_types(void)
{
    type_register_static(&via_ide_info);
}

type_init(via_ide_register_types)
