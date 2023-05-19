/*
 * QEMU PCI PS/2 adapter.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/pci/pci_device.h"
#include "hw/input/ps2pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qemu/log.h"

static const VMStateDescription vmstate_ps2_pci = {
    .name = "ps2-pci",
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PS2PCIState),
        VMSTATE_END_OF_LIST()
    }
};

#define PS2_CTRL		(0)
#define PS2_STATUS		(1)
#define PS2_DATA		(2)

#define PS2_CTRL_CLK		(1<<0)
#define PS2_CTRL_DAT		(1<<1)
#define PS2_CTRL_TXIRQ		(1<<2)
#define PS2_CTRL_ENABLE		(1<<3)
#define PS2_CTRL_RXIRQ		(1<<4)

#define PS2_STAT_CLK		(1<<0)
#define PS2_STAT_DAT		(1<<1)
#define PS2_STAT_PARITY		(1<<2)
#define PS2_STAT_RXFULL		(1<<5)
#define PS2_STAT_TXBUSY		(1<<6)
#define PS2_STAT_TXEMPTY	(1<<7)

static void ps2_pci_update_irq(PS2PCIState *s)
{
    int level = (s->pending && (s->cr & PS2_CTRL_RXIRQ) != 0)
                 || (s->cr & PS2_CTRL_TXIRQ) != 0;

    qemu_set_irq(s->irq, level);
}

static void ps2_pci_set_irq(void *opaque, int n, int level)
{
    PS2PCIState *s = (PS2PCIState *)opaque;

    s->pending = level;
    ps2_pci_update_irq(s);
}

static uint64_t ps2_pci_io_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PS2PCIState *s = (PS2PCIState*)opaque;
    switch (offset) {
    case PS2_CTRL: /* CTRL */
        return s->cr;
    case PS2_STATUS: /* STATUS */
        {
            uint32_t stat = 0;
            if (s->pending)
                stat = PS2_STAT_RXFULL;
            else
                stat = PS2_STAT_TXEMPTY;
            uint8_t val = 0;
            val = s->last;
            val = val ^ (val >> 4);
            val = val ^ (val >> 2);
            val = (val ^ (val >> 1)) & 1;
            if (val) {
                stat |= PS2_STAT_PARITY;
            }
            return stat;
        }
    case PS2_DATA: /* DATA */
        if (s->pending && s->cr & PS2_CTRL_ENABLE) {
            s->last = ps2_read_data(s->ps2dev);
            if (ps2_queue_empty(s->ps2dev))
                s->pending = 0;
        } else {
            return 0;
        }
        return s->last;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ps2_pci_io_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void ps2_pci_io_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PS2PCIState *s = (PS2PCIState *)opaque;
    switch (offset) {
    case PS2_CTRL: /* CTRL */
        s->cr = value;
        break;
    case PS2_STATUS: /* STATUS */
        break;
    case PS2_DATA: /* DATA */
        if (s->cr & PS2_CTRL_ENABLE) {
            if (s->is_mouse) {
                ps2_write_mouse(PS2_MOUSE_DEVICE(s->ps2dev), value);
            } else {
                ps2_write_keyboard(PS2_KBD_DEVICE(s->ps2dev), value);
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ps2_pci_io_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps ps2_pci_io_ops = {
    .read = ps2_pci_io_read,
    .write = ps2_pci_io_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ps2_pci_realize_common(PCIDevice *dev, Error **errp)
{
    PS2PCIState *s = PS2_PCI(dev);
    Object *obj = OBJECT(dev);
    int ret;

    uint8_t *pci_conf = dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */

    s->irq = pci_allocate_irq(&s->parent_obj);
    memory_region_init_io(&s->io, obj, &ps2_pci_io_ops, s,
                          "ps2-pci-io", 16);
    pci_set_byte(&s->parent_obj.config[PCI_REVISION_ID], 0);
    pci_register_bar(&s->parent_obj, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);

    if (pci_bus_is_express(pci_get_bus(dev))) {
        ret = pcie_endpoint_cap_init(dev, 0x80);
        assert(ret > 0);
    } else {
        dev->cap_present &= ~QEMU_PCI_CAP_EXPRESS;
    }
    qdev_connect_gpio_out(DEVICE(s->ps2dev), PS2_DEVICE_IRQ,
                       qdev_get_gpio_in_named((DeviceState*)dev, "ps2-input-irq", 0));
}

static void ps2_pci_keyboard_realize(PCIDevice *dev, Error **errp)
{
    PS2PCIKbdState *s = PS2_PCI_KBD_DEVICE(dev);
    PS2PCIState *ps = PS2_PCI(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->kbd), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->kbd);
    ps2_pci_realize_common(dev, errp);
}

static void ps2_pci_mouse_realize(PCIDevice *dev, Error **errp)
{
    PS2PCIMouseState *s = PS2_PCI_MOUSE_DEVICE(dev);
    PS2PCIState *ps = PS2_PCI(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mouse), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->mouse);
    ps2_pci_realize_common(dev, errp);
}

static void ps2_pci_kbd_init(Object *obj)
{
    PCIDevice *dev = PCI_DEVICE(obj);
    PS2PCIKbdState *s = PS2_PCI_KBD_DEVICE(obj);
    PS2PCIState *ps = PS2_PCI(obj);

    ps->is_mouse = false;
    object_initialize_child(obj, "kbd", &s->kbd, TYPE_PS2_KBD_DEVICE);

    dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    
    qdev_init_gpio_out(DEVICE(obj), &ps->irq, 1);
    qdev_init_gpio_in_named(DEVICE(obj), ps2_pci_set_irq,
                            "ps2-input-irq", 1);
}

static void ps2_pci_mouse_init(Object *obj)
{
    PCIDevice *dev = PCI_DEVICE(obj);
    PS2PCIMouseState *s = PS2_PCI_MOUSE_DEVICE(obj);
    PS2PCIState *ps = PS2_PCI(obj);

    ps->is_mouse = true;
    object_initialize_child(obj, "mouse", &s->mouse, TYPE_PS2_MOUSE_DEVICE);

    dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    
    qdev_init_gpio_out(DEVICE(obj), &ps->irq, 1);
    qdev_init_gpio_in_named(DEVICE(obj), ps2_pci_set_irq,
                            "ps2-input-irq", 1);
}

static void ps2_pci_keyboard_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->class_id  = PCI_CLASS_INPUT_KEYBOARD;
    k->vendor_id = 0x14f2;
    k->device_id = 0x0123;

    k->realize   = ps2_pci_keyboard_realize;
    k->exit      = NULL;
    dc->vmsd     = &vmstate_ps2_pci;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void ps2_pci_mouse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->class_id  = PCI_CLASS_INPUT_MOUSE;
    k->vendor_id = 0x14f2;
    k->device_id = 0x0124;

    k->realize   = ps2_pci_mouse_realize;
    k->exit      = NULL;
    dc->vmsd     = &vmstate_ps2_pci;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo ps2_pci_keyboard_type_info = {
    .name           = TYPE_PS2_PCI_KBD_DEVICE,
    .parent         = TYPE_PS2_PCI,
    .instance_size  = sizeof(PS2PCIKbdState),
    .instance_init  = ps2_pci_kbd_init,
    .class_init     = ps2_pci_keyboard_class_init,
};

static const TypeInfo ps2_pci_mouse_type_info = {
    .name           = TYPE_PS2_PCI_MOUSE_DEVICE,
    .parent         = TYPE_PS2_PCI,
    .instance_size  = sizeof(PS2PCIMouseState),
    .instance_init  = ps2_pci_mouse_init,
    .class_init     = ps2_pci_mouse_class_init,
};

static const TypeInfo ps2_pci_type_info = {
    .name           = TYPE_PS2_PCI,
    .parent         = TYPE_PCI_DEVICE,
    .instance_size  = sizeof(PS2PCIState),
    .abstract       = true,
    .interfaces     = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void ps2_pci_register_types(void)
{
    type_register_static(&ps2_pci_keyboard_type_info);
    type_register_static(&ps2_pci_mouse_type_info);
    type_register_static(&ps2_pci_type_info);
}

type_init(ps2_pci_register_types)
