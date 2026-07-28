/*
 * xiic_fpga_i2c.c - QEMU model of a PCIe FPGA that embeds Xilinx AXI-IIC
 *                   controllers behind a shared MSI interrupt aggregator.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/host-utils.h"
#include "migration/vmstate.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/xlnx-axi-iic.h"

#define TYPE_XIIC_FPGA_I2C "xiic-fpga-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(XiicFpgaI2cState, XIIC_FPGA_I2C)

#define XIIC_FPGA_MAX_CHANNELS 32

#define XIIC_FPGA_IRQ_STATUS_REG 0
#define XIIC_FPGA_IRQ_UNMASK_REG 4
#define XIIC_FPGA_IRQ_REGION_SIZE 8

struct XiicFpgaI2cState {
    PCIDevice parent_obj;

    MemoryRegion bar0;
    MemoryRegion irq_mmio;

    uint32_t num_channels;
    uint32_t ch_base_offset;
    uint32_t ch_stride;
    uint32_t bar_size;
    uint32_t num_msi_vectors;

    uint32_t irq_status_offset;
    uint32_t irq_unmask_offset;
    uint32_t irq_msi_vector;

    uint32_t irq_status;
    uint32_t irq_unmask;
    bool msi_asserted;

    XlnxAxiIicState chan[XIIC_FPGA_MAX_CHANNELS];
};

static void xiic_fpga_update_msi(XiicFpgaI2cState *s)
{
    PCIDevice *pci_dev = PCI_DEVICE(s);
    uint32_t active = s->irq_status & s->irq_unmask;

    if (msi_enabled(pci_dev)) {
        if (active && !s->msi_asserted) {
            msi_notify(pci_dev, s->irq_msi_vector);
            s->msi_asserted = true;
        } else if (!active) {
            s->msi_asserted = false;
        }
        return;
    }

    pci_set_irq(pci_dev, active != 0);
}

static void xiic_fpga_irq_set(void *opaque, int n, int level)
{
    XiicFpgaI2cState *s = opaque;

    if (level) {
        s->irq_status |= (1u << n);
    } else {
        s->irq_status &= ~(1u << n);
    }
    xiic_fpga_update_msi(s);
}

static uint64_t xiic_fpga_irq_read(void *opaque, hwaddr addr, unsigned size)
{
    XiicFpgaI2cState *s = opaque;

    switch (addr) {
    case XIIC_FPGA_IRQ_STATUS_REG:
        return s->irq_status;
    case XIIC_FPGA_IRQ_UNMASK_REG:
        return s->irq_unmask;
    default:
        return 0;
    }
}

static void xiic_fpga_irq_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    XiicFpgaI2cState *s = opaque;

    switch (addr) {
    case XIIC_FPGA_IRQ_STATUS_REG:
        s->msi_asserted = false;
        xiic_fpga_update_msi(s);
        break;
    case XIIC_FPGA_IRQ_UNMASK_REG:
        s->irq_unmask = val;
        xiic_fpga_update_msi(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps xiic_fpga_irq_ops = {
    .read = xiic_fpga_irq_read,
    .write = xiic_fpga_irq_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void xiic_fpga_i2c_realize(PCIDevice *pci_dev, Error **errp)
{
    XiicFpgaI2cState *s = XIIC_FPGA_I2C(pci_dev);

    if (s->num_channels < 1 || s->num_channels > XIIC_FPGA_MAX_CHANNELS) {
        error_setg(errp, "num-channels must be between 1 and %d",
                   XIIC_FPGA_MAX_CHANNELS);
        return;
    }
    if (s->irq_unmask_offset != s->irq_status_offset + 4) {
        error_setg(errp, "irq-unmask-offset must be irq-status-offset + 4");
        return;
    }

    if (s->bar_size < s->ch_base_offset + s->num_channels * s->ch_stride) {
        s->bar_size = s->ch_base_offset + s->num_channels * s->ch_stride;
    }
    if (s->bar_size < s->irq_status_offset + XIIC_FPGA_IRQ_REGION_SIZE) {
        s->bar_size = s->irq_status_offset + XIIC_FPGA_IRQ_REGION_SIZE;
    }
    s->bar_size = pow2ceil(s->bar_size);

    s->num_msi_vectors = pow2ceil(s->num_channels);
    if (s->num_msi_vectors > 32) {
        s->num_msi_vectors = 32;
    }
    if (s->irq_msi_vector >= s->num_msi_vectors) {
        error_setg(errp, "irq-msi-vector %u out of range (0..%u)",
                   s->irq_msi_vector, s->num_msi_vectors - 1);
        return;
    }

    memory_region_init(&s->bar0, OBJECT(s), "xiic-fpga-i2c-bar0", s->bar_size);

    qdev_init_gpio_in(DEVICE(s), xiic_fpga_irq_set, s->num_channels);

    for (unsigned i = 0; i < s->num_channels; i++) {
        g_autofree char *name = g_strdup_printf("channel[%u]", i);
        g_autofree char *bus_name = g_strdup_printf("xiic-fpga-i2c.%u", i);
        object_initialize_child(OBJECT(s), name, &s->chan[i],
                                TYPE_XLNX_AXI_IIC);
        qdev_prop_set_string(DEVICE(&s->chan[i]), "bus-name", bus_name);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->chan[i]), errp)) {
            return;
        }
        memory_region_add_subregion(&s->bar0,
            s->ch_base_offset + i * s->ch_stride,
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->chan[i]), 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->chan[i]), 0,
                           qdev_get_gpio_in(DEVICE(s), i));
    }

    memory_region_init_io(&s->irq_mmio, OBJECT(s), &xiic_fpga_irq_ops, s,
                          "xiic-fpga-i2c-irq", XIIC_FPGA_IRQ_REGION_SIZE);
    memory_region_add_subregion(&s->bar0, s->irq_status_offset, &s->irq_mmio);

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    pci_config_set_interrupt_pin(pci_dev->config, 1);

    if (msi_init(pci_dev, 0, s->num_msi_vectors, true, false, errp) < 0) {
        error_prepend(errp, "xiic-fpga-i2c: failed to init MSI: ");
        return;
    }
}

static void xiic_fpga_i2c_exit(PCIDevice *pci_dev)
{
    msi_uninit(pci_dev);
}

static void xiic_fpga_i2c_reset_hold(Object *obj, ResetType type)
{
    XiicFpgaI2cState *s = XIIC_FPGA_I2C(obj);

    s->irq_status = 0;
    s->irq_unmask = 0;
    s->msi_asserted = false;
}

static const VMStateDescription vmstate_xiic_fpga_i2c = {
    .name = "xiic-fpga-i2c",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, XiicFpgaI2cState),
        VMSTATE_UINT32(irq_status, XiicFpgaI2cState),
        VMSTATE_UINT32(irq_unmask, XiicFpgaI2cState),
        VMSTATE_BOOL(msi_asserted, XiicFpgaI2cState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property xiic_fpga_i2c_props[] = {
    DEFINE_PROP_UINT32("num-channels", XiicFpgaI2cState, num_channels, 4),
    DEFINE_PROP_UINT32("ch-base-offset", XiicFpgaI2cState, ch_base_offset, 0x0),
    DEFINE_PROP_UINT32("ch-stride", XiicFpgaI2cState, ch_stride, 0x1000),
    DEFINE_PROP_UINT32("bar-size", XiicFpgaI2cState, bar_size, 0x8000),
    DEFINE_PROP_UINT32("irq-status-offset", XiicFpgaI2cState,
                       irq_status_offset, 0x6000),
    DEFINE_PROP_UINT32("irq-unmask-offset", XiicFpgaI2cState,
                       irq_unmask_offset, 0x6004),
    DEFINE_PROP_UINT32("irq-msi-vector", XiicFpgaI2cState, irq_msi_vector, 0),
};

static void xiic_fpga_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->realize   = xiic_fpga_i2c_realize;
    k->exit      = xiic_fpga_i2c_exit;
    k->vendor_id = 0x10ee;
    k->device_id = 0x7021;
    k->revision  = 0x01;
    k->class_id  = PCI_CLASS_OTHERS;

    rc->phases.hold = xiic_fpga_i2c_reset_hold;
    dc->vmsd = &vmstate_xiic_fpga_i2c;
    dc->desc = "FPGA I2C (Xilinx AXI-IIC) emulation";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, xiic_fpga_i2c_props);
}

static const TypeInfo xiic_fpga_i2c_info = {
    .name          = TYPE_XIIC_FPGA_I2C,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XiicFpgaI2cState),
    .class_init    = xiic_fpga_i2c_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void xiic_fpga_i2c_register_types(void)
{
    type_register_static(&xiic_fpga_i2c_info);
}

type_init(xiic_fpga_i2c_register_types)
