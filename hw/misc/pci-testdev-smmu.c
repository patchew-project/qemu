/*
 * QEMU PCI test device
 *
 * Copyright (c) 2012 Red Hat Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "qemu/event_notifier.h"

#include "pci-testdev-smmu.h"

/*
 * pci-testdev-smmu:
 *          Simple PCIe device, to enable read and write from memory.
 * Architecture:
 *          Following registers are supported.
 *          TST_COMMAND = 0x0
 *          TST_STATUS  = 0x4
 *          TST_SRC_ADDRESS = 0x8
 *          TST_SIZE        = 0x10
 *          TST_DST_ADDRESS = 0x18
 */
#define PCI_TSTDEV_NREGS 0x10

/*
 *  TST_COMMAND Register bits
 *      OP[0]
 *          READ = 0x0
 *          WRITE = 0x1
 */

struct RegInfo {
        uint64_t data;
        char *name;
};
typedef struct RegInfo RegInfo;

typedef struct PCITestDevState {
    /*< private >*/
    PCIDevice dev;
    /*< public >*/

    MemoryRegion mmio;
    RegInfo regs[PCI_TSTDEV_NREGS];
} PCITestDevState;

#define TYPE_PCI_TEST_DEV "pci-testdev-smmu"

#define PCI_TEST_DEV(obj) \
    OBJECT_CHECK(PCITestDevState, (obj), TYPE_PCI_TEST_DEV)

static void
pci_tstdev_reset(PCITestDevState *d)
{
    memset(d->regs, 0, sizeof(d->regs));
}

static inline void
pci_tstdev_write_reg(PCITestDevState *pdev, hwaddr addr, uint64_t val)
{
    RegInfo *reg = &pdev->regs[addr >> 2];
    reg->data = val;
}

static inline uint32_t
pci_tstdev_read32_reg(PCITestDevState *pdev, hwaddr addr)
{
    RegInfo *reg = &pdev->regs[addr >> 2];
    return (uint32_t) reg->data;
}

static inline uint64_t
pci_tstdev_read64_reg(PCITestDevState *pdev, hwaddr addr)
{
        RegInfo *reg = &pdev->regs[addr >> 2];
        return reg->data;
}

static void
pci_tstdev_handle_cmd(PCITestDevState *pdev, hwaddr addr, uint64_t val,
                            unsigned _unused_size)
{
    uint64_t s = pci_tstdev_read64_reg(pdev, TST_REG_SRC_ADDR);
    uint64_t d = pci_tstdev_read64_reg(pdev, TST_REG_DST_ADDR);
    uint32_t size = pci_tstdev_read32_reg(pdev, TST_REG_SIZE);
    uint8_t buf[128];

    printf("+++++++++++++++++++++> src:%lx, dst:%lx size:%d\n",
           s, d, size);
    while (size) {
        int nbytes = (size < sizeof(buf)) ? size: sizeof(buf);
        int ret = 0;
        printf("nbytes:%d\n", nbytes);
        if (val & CMD_READ) {
            printf("doing pci_dma_read\n");
            ret = pci_dma_read(&pdev->dev, s, (void*)buf, nbytes);
        }
        if (ret)
            return;

        if (val & CMD_WRITE) {
            printf("doing pci_dma_write\n");
            ret = pci_dma_write(&pdev->dev, d, (void*)buf, nbytes);
        }
        size -= nbytes;
        s += nbytes;
        d += nbytes;
    }
}

static void
pci_tstdev_mmio_write(void *opaque, hwaddr addr,
                      uint64_t val, unsigned size)
{
    PCITestDevState *d = opaque;
    uint64_t lo;

    printf("=================> addr:%ld act:%d val:%lx reg_addr:%p\n",
           addr, TST_REG_COMMAND, val, &d->regs[addr]);
    //addr >>= 2;
    switch (addr) {
    case TST_REG_COMMAND:
            printf("calling handler.....\n");
            pci_tstdev_handle_cmd(d, addr, val, size);
    case TST_REG_SRC_ADDR:
    case TST_REG_DST_ADDR:
    case TST_REG_SIZE:
            pci_tstdev_write_reg(d, addr, val);
            break;
    case TST_REG_SRC_ADDR + 4:
    case TST_REG_DST_ADDR + 4:
            lo = pci_tstdev_read32_reg(d, addr);
            lo &= (0xffffffffULL << 32);
            pci_tstdev_write_reg(d, addr, (val << 32) | lo);
            break;
    case TST_REG_STATUS:        /* Read only reg */
    default:
        printf("Unkown/RO register write\n");
        break;
    }
}

static uint64_t
pci_tstdev_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCITestDevState *d = opaque;

    switch (addr) {
    case TST_REG_SRC_ADDR:
    case TST_REG_DST_ADDR:
            return pci_tstdev_read64_reg(d, addr);
    }

    return pci_tstdev_read32_reg(d, addr);
}

static const MemoryRegionOps pci_testdev_mmio_ops = {
    .read = pci_tstdev_mmio_read,
    .write = pci_tstdev_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void pci_tstdev_realize(PCIDevice *pci_dev, Error **errp)
{
    PCITestDevState *d = PCI_TEST_DEV(pci_dev);
    uint8_t *pci_conf;

    pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 0; /* no interrupt pin */

    memory_region_init_io(&d->mmio, OBJECT(d), &pci_testdev_mmio_ops, d,
                          "pci-testdev-smmu-mmio", 1 << 10);

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
}

static void
pci_tstdev_uninit(PCIDevice *dev)
{
    PCITestDevState *d = PCI_TEST_DEV(dev);

    pci_tstdev_reset(d);
}

static void qdev_pci_tstdev_reset(DeviceState *dev)
{
    PCITestDevState *d = PCI_TEST_DEV(dev);
    pci_tstdev_reset(d);
}

static void pci_tstdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_tstdev_realize;
    k->exit = pci_tstdev_uninit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_TEST;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_OTHERS;
    dc->desc = "PCI Test Device - for smmu";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = qdev_pci_tstdev_reset;
}

static const TypeInfo pci_tstdev_info = {
    .name          = TYPE_PCI_TEST_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCITestDevState),
    .class_init    = pci_tstdev_class_init,
};

static void pci_tstdev_register_types(void)
{
    type_register_static(&pci_tstdev_info);
}

type_init(pci_tstdev_register_types)
