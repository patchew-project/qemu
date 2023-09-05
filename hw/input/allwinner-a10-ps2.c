/*
 * Allwinner A10 PS2 Module emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/input/allwinner-a10-ps2.h"
#include "hw/input/ps2.h"
#include "hw/irq.h"

/* PS2 register offsets */
enum {
    REG_GCTL    = 0x0000, /* Global Control Reg */
    REG_DATA    = 0x0004, /* Data Reg */
    REG_LCTL    = 0x0008, /* Line Control Reg */
    REG_LSTS    = 0x000C, /* Line Status Reg */
    REG_FCTL    = 0x0010, /* FIFO Control Reg */
    REG_FSTS    = 0x0014, /* FIFO Status Reg */
    REG_CLKDR   = 0x0018, /* Clock Divider Reg */
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* PS2 register reset values */
enum {
    REG_GCTL_RST    = 0x00000002,
    REG_DATA_RST    = 0x00000000,
    REG_LCTL_RST    = 0x00000000,
    REG_LSTS_RST    = 0x00030000,
    REG_FCTL_RST    = 0x00000000,
    REG_FSTS_RST    = 0x00000100,
    REG_CLKDR_RST   = 0x00002F4F,
};

/* REG_GCTL Fields */
#define FIELD_REG_GCTL_SOFT_RST     (1 << 2)
#define FIELD_REG_GCTL_INT_EN       (1 << 3)
#define FIELD_REG_GCTL_INT_FLAG     (1 << 4)

/* REG_FCTL Fields */
#define FIELD_REG_FCTL_RXRDY_IEN    (1 << 0)
#define FIELD_REG_FCTL_TXRDY_IEN    (1 << 8)

/* REG_FSTS Fields */
#define FIELD_REG_FSTS_RX_RDY       (1 << 0)
#define FIELD_REG_FSTS_TX_RDY       (1 << 8)
#define FIELD_REG_FSTS_RX_LEVEL1    (1 << 16)

static int allwinner_a10_ps2_fctl_is_irq(AwA10PS2State *s)
{
    return (s->regs[REG_INDEX(REG_FCTL)] & FIELD_REG_FCTL_TXRDY_IEN) ||
        (s->pending &&
         (s->regs[REG_INDEX(REG_FCTL)] & FIELD_REG_FCTL_RXRDY_IEN));
}

static void allwinner_a10_ps2_update_irq(AwA10PS2State *s)
{
    int level = (s->regs[REG_INDEX(REG_GCTL)] & FIELD_REG_GCTL_INT_EN) &&
        allwinner_a10_ps2_fctl_is_irq(s);

    qemu_set_irq(s->irq, level);
}

static void allwinner_a10_ps2_set_irq(void *opaque, int n, int level)
{
    AwA10PS2State *s = (AwA10PS2State *)opaque;

    s->pending = level;
    allwinner_a10_ps2_update_irq(s);
}

static uint64_t allwinner_a10_ps2_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    AwA10PS2State *s = AW_A10_PS2(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_FSTS:
        {
            uint32_t stat = FIELD_REG_FSTS_TX_RDY;
            if (s->pending) {
                stat |= FIELD_REG_FSTS_RX_LEVEL1 | FIELD_REG_FSTS_RX_RDY;
            }
            return stat;
        }
        break;
    case REG_DATA:
        if (s->pending) {
            s->last = ps2_read_data(s->ps2dev);
        }
        return s->last;
    case REG_GCTL:
        {
            if (allwinner_a10_ps2_fctl_is_irq(s)) {
                s->regs[idx] |= FIELD_REG_GCTL_INT_FLAG;
            } else {
                s->regs[idx] &= FIELD_REG_GCTL_INT_FLAG;
            }
        }
        break;
    case REG_LCTL:
    case REG_LSTS:
    case REG_FCTL:
    case REG_CLKDR:
        break;
    case 0x1C ... AW_A10_PS2_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void allwinner_a10_ps2_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwA10PS2State *s = AW_A10_PS2(opaque);
    const uint32_t idx = REG_INDEX(offset);

    s->regs[idx] = (uint32_t) val;

    switch (offset) {
    case REG_GCTL:
        allwinner_a10_ps2_update_irq(s);
        s->regs[idx] &= ~FIELD_REG_GCTL_SOFT_RST;
        break;
    case REG_DATA:
        /* ??? This should toggle the TX interrupt line.  */
        /* ??? This means kbd/mouse can block each other.  */
        if (s->is_mouse) {
            ps2_write_mouse(PS2_MOUSE_DEVICE(s->ps2dev), val);
        } else {
            ps2_write_keyboard(PS2_KBD_DEVICE(s->ps2dev), val);
        }
        break;
    case REG_LCTL:
    case REG_LSTS:
    case REG_FCTL:
    case REG_FSTS:
    case REG_CLKDR:
        break;
    case 0x1C ... AW_A10_PS2_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }
}

static const MemoryRegionOps allwinner_a10_ps2_ops = {
    .read = allwinner_a10_ps2_read,
    .write = allwinner_a10_ps2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const VMStateDescription allwinner_a10_ps2_vmstate = {
    .name = "allwinner-a10-ps2",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10PS2State, AW_A10_PS2_REGS_NUM),
        VMSTATE_INT32(pending, AwA10PS2State),
        VMSTATE_UINT32(last, AwA10PS2State),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_a10_ps2_realize(DeviceState *dev, Error **errp)
{
    AwA10PS2State *s = AW_A10_PS2(dev);

    qdev_connect_gpio_out(DEVICE(s->ps2dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-input-irq", 0));
}

static void allwinner_a10_ps2_kbd_realize(DeviceState *dev, Error **errp)
{
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_GET_CLASS(dev);
    AwA10PS2KbdState *s = AW_A10_PS2_KBD_DEVICE(dev);
    AwA10PS2State *ps = AW_A10_PS2(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->kbd), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->kbd);
    pdc->parent_realize(dev, errp);
}

static void allwinner_a10_ps2_kbd_init(Object *obj)
{
    AwA10PS2KbdState *s = AW_A10_PS2_KBD_DEVICE(obj);
    AwA10PS2State *ps = AW_A10_PS2(obj);

    ps->is_mouse = false;
    object_initialize_child(obj, "kbd", &s->kbd, TYPE_PS2_KBD_DEVICE);
}

static void allwinner_a10_ps2_mouse_realize(DeviceState *dev, Error **errp)
{
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_GET_CLASS(dev);
    AwA10PS2MouseState *s = AW_A10_PS2_MOUSE_DEVICE(dev);
    AwA10PS2State *ps = AW_A10_PS2(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mouse), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->mouse);
    pdc->parent_realize(dev, errp);
}

static void allwinner_a10_ps2_mouse_init(Object *obj)
{
    AwA10PS2MouseState *s = AW_A10_PS2_MOUSE_DEVICE(obj);
    AwA10PS2State *ps = AW_A10_PS2(obj);

    ps->is_mouse = true;
    object_initialize_child(obj, "mouse", &s->mouse, TYPE_PS2_MOUSE_DEVICE);
}

static void allwinner_a10_ps2_kbd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_CLASS(oc);

    device_class_set_parent_realize(dc, allwinner_a10_ps2_kbd_realize,
                                    &pdc->parent_realize);
}

static const TypeInfo allwinner_a10_ps2_kbd_info = {
    .name          = TYPE_AW_A10_PS2_KBD_DEVICE,
    .parent        = TYPE_AW_A10_PS2,
    .instance_init = allwinner_a10_ps2_kbd_init,
    .instance_size = sizeof(AwA10PS2KbdState),
    .class_init    = allwinner_a10_ps2_kbd_class_init,
};

static void allwinner_a10_ps2_mouse_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_CLASS(oc);

    device_class_set_parent_realize(dc, allwinner_a10_ps2_mouse_realize,
                                    &pdc->parent_realize);
}

static const TypeInfo allwinner_a10_ps2_mouse_info = {
    .name          = TYPE_AW_A10_PS2_MOUSE_DEVICE,
    .parent        = TYPE_AW_A10_PS2,
    .instance_init = allwinner_a10_ps2_mouse_init,
    .instance_size = sizeof(AwA10PS2MouseState),
    .class_init    = allwinner_a10_ps2_mouse_class_init,
};

static void allwinner_a10_ps2_reset_enter(Object *obj, ResetType type)
{
    AwA10PS2State *s = AW_A10_PS2(obj);

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_GCTL)] = REG_GCTL_RST;
    s->regs[REG_INDEX(REG_DATA)] = REG_DATA_RST;
    s->regs[REG_INDEX(REG_LCTL)] = REG_LCTL_RST;
    s->regs[REG_INDEX(REG_LSTS)] = REG_LSTS_RST;
    s->regs[REG_INDEX(REG_FCTL)] = REG_FCTL_RST;
    s->regs[REG_INDEX(REG_FSTS)] = REG_FSTS_RST;
    s->regs[REG_INDEX(REG_CLKDR)] = REG_CLKDR_RST;
}

static void allwinner_a10_ps2_init(Object *obj)
{
    AwA10PS2State *s = AW_A10_PS2(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &allwinner_a10_ps2_ops, s,
                          "allwinner-a10-ps2", AW_A10_PS2_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), allwinner_a10_ps2_set_irq,
                            "ps2-input-irq", 1);
}

static void allwinner_a10_ps2_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.enter = allwinner_a10_ps2_reset_enter;
    dc->realize = allwinner_a10_ps2_realize;
    dc->vmsd = &allwinner_a10_ps2_vmstate;
}

static const TypeInfo allwinner_a10_ps2_type_info = {
    .name          = TYPE_AW_A10_PS2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_ps2_init,
    .instance_size = sizeof(AwA10PS2State),
    .class_init    = allwinner_a10_ps2_class_init,
    .class_size    = sizeof(AwA10PS2DeviceClass),
    .abstract      = true,
    .class_init    = allwinner_a10_ps2_class_init,
};

static void allwinner_a10_ps2_register_types(void)
{
    type_register_static(&allwinner_a10_ps2_type_info);
    type_register_static(&allwinner_a10_ps2_kbd_info);
    type_register_static(&allwinner_a10_ps2_mouse_info);
}

type_init(allwinner_a10_ps2_register_types)
