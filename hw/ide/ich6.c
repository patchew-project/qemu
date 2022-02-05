/*
 * QEMU IDE Emulation: PCI ICH6/ICH7 IDE support.
 *
 * Copyright (c) 2022 Liav Albani
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
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/dma.h"

#include "hw/ide/pci.h"
#include "trace.h"



static const MemoryRegionOps ich6_bmdma_ops = {
    .read = piix_bmdma_read,
    .write = piix_bmdma_write,
};

static void bmdma_setup_bar(PCIIDEState *d)
{
    int i;

    memory_region_init(&d->bmdma_bar, OBJECT(d), "ich6-bmdma-container", 16);
    for (i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        memory_region_init_io(&bm->extra_io, OBJECT(d), &ich6_bmdma_ops, bm,
                              "ich6-bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8, &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, OBJECT(d),
                              &bmdma_addr_ioport_ops, bm, "bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8 + 4, &bm->addr_ioport);
    }
}

static uint32_t ich6_pci_config_read(PCIDevice *d,
                                       uint32_t address, int len)
{
    return pci_default_read_config(d, address, len);
}

static void ich6_pci_config_write(PCIDevice *d, uint32_t addr, uint32_t val,
                                    int l)
{
    uint32_t i;

    pci_default_write_config(d, addr, val, l);

    for (i = addr; i < addr + l; i++) {
        switch (i) {
        case 0x40:
            pci_default_write_config(d, i, 0x8000, 2);
            continue;
        case 0x42:
            pci_default_write_config(d, i, 0x8000, 2);
            continue;
        }
    }
}

static void ich6_ide_reset(DeviceState *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    PCIDevice *pd = PCI_DEVICE(d);
    uint8_t *pci_conf = pd->config;
    int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }

    /* TODO: this is the default. do not override. */
    pci_conf[PCI_COMMAND] = 0x00;
    /* TODO: this is the default. do not override. */
    pci_conf[PCI_COMMAND + 1] = 0x00;
    /* TODO: use pci_set_word */
    pci_conf[PCI_STATUS] = PCI_STATUS_FAST_BACK;
    pci_conf[PCI_STATUS + 1] = PCI_STATUS_DEVSEL_MEDIUM >> 8;
    pci_conf[0x20] = 0x01; /* BMIBA: 20-23h */
}

static int pci_ich6_init_ports(PCIIDEState *d)
{
    int i;

    for (i = 0; i < 2; i++) {
        ide_bus_init(&d->bus[i], sizeof(d->bus[i]), DEVICE(d), i, 2);
        ide_init2(&d->bus[i], d->native_irq);

        bmdma_init(&d->bus[i], &d->bmdma[i], d);
        d->bmdma[i].bus = &d->bus[i];
        ide_register_restart_cb(&d->bus[i]);
    }

    return 0;
}

static void pci_ich6_ide_realize(PCIDevice *dev, Error **errp)
{
    PCIIDEState *d = PCI_IDE(dev);
    uint8_t *pci_conf = dev->config;
    int rc;

    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */

    /* PCI native mode-only controller, supports bus mastering */
    pci_conf[PCI_CLASS_PROG] = 0x85;

    bmdma_setup_bar(d);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);

    d->native_irq = pci_allocate_irq(&d->parent_obj);
    /* Address Map Register - Non Combined Mode, MAP.USCC = 0 */
    pci_conf[0x90]   = 0;

    /* IDE Decode enabled by default */
    pci_set_long(pci_conf + 0x40, 0x80008000);

    /* IDE Timing control - Disable UDMA controls */
    pci_set_long(pci_conf + 0x48, 0x00000000);

    vmstate_register(VMSTATE_IF(dev), 0, &vmstate_ide_pci, d);

    memory_region_init_io(&d->data_bar[0], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[0], "ich6-ide0-data", 8);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[0]);

    memory_region_init_io(&d->cmd_bar[0], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[0], "ich6-ide0-cmd", 4);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[0]);

    memory_region_init_io(&d->data_bar[1], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[1], "ich6-ide1-data", 8);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[1]);

    memory_region_init_io(&d->cmd_bar[1], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[1], "ich6-ide1-cmd", 4);
    pci_register_bar(dev, 3, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[1]);

    rc = pci_ich6_init_ports(d);
    if (rc) {
        error_setg_errno(errp, -rc, "Failed to realize %s",
                         object_get_typename(OBJECT(dev)));
    }
}

static void pci_ich6_ide_exitfn(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    unsigned i;

    for (i = 0; i < 2; ++i) {
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
    }
}

static void ich6_ide_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->reset = ich6_ide_reset;
    k->realize = pci_ich6_ide_realize;
    k->exit = pci_ich6_ide_exitfn;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82801GB;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    k->config_read = ich6_pci_config_read;
    k->config_write = ich6_pci_config_write;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->hotpluggable = false;
}

static const TypeInfo ich6_ide_info = {
    .name          = "ich6-ide",
    .parent        = TYPE_PCI_IDE,
    .class_init    = ich6_ide_class_init,
};

static void ich6_ide_register_types(void)
{
    type_register_static(&ich6_ide_info);
}

type_init(ich6_ide_register_types)
