/*
 * Allwinner A10 HDMI Module emulation
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
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "hw/display/allwinner-a10-hdmi.h"
#include "trace.h"

/* HDMI register offsets */
enum {
    REG_HPD                 = 0x000C, /* HDMI Hotplug detect */
    REG_DDC_CTRL            = 0x0500, /* DDC Control */
    REG_DDC_SLAVE_ADDRESS   = 0x0504, /* DDC Slave address */
    REG_DDC_INT_STATUS      = 0x050C, /* DDC Interrupt status */
    REG_DDC_FIFO_CTRL       = 0x0510, /* DDC FIFO Control */
    REG_DDC_FIFO_ACCESS     = 0x0518, /* DDC FIFO access */
    REG_DDC_COMMAND         = 0x0520, /* DDC Command */
};

/* HPD register fields */
#define FIELD_HPD_HOTPLUG_DET_HIGH      (1 << 0)

/* DDC_CTRL register fields */
#define FIELD_DDC_CTRL_SW_RST           (1 << 0)
#define FIELD_DDC_CTRL_ACCESS_CMD_START (1 << 30)

/* FIFO_CTRL register fields */
#define FIELD_FIFO_CTRL_ADDRESS_CLEAR   (1 << 31)

/* DDC_SLAVE_ADDRESS register fields */
#define FIELD_DDC_SLAVE_ADDRESS_SEGMENT_SHIFT   (24)
#define FIELD_DDC_SLAVE_ADDRESS_OFFSET_SHIFT    (8)

/* DDC_INT_STATUS register fields */
#define FIELD_DDC_INT_STATUS_TRANSFER_COMPLETE  (1 << 0)

/* DDC access command */
enum {
    DDC_COMMAND_E_DDC_READ = 6,
};



#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

static uint64_t allwinner_a10_hdmi_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    AwA10HdmiState *s = AW_A10_HDMI(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = s->regs[idx];

    switch (offset) {
    case REG_HPD:
        val = FIELD_HPD_HOTPLUG_DET_HIGH;
        break;
    case REG_DDC_FIFO_ACCESS:
        val = s->edid_blob[s->edid_reg % sizeof(s->edid_blob)];
        s->edid_reg++;
        break;
    case 0x544 ... AW_A10_HDMI_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        return 0;
    default:
        break;
    }

    trace_allwinner_a10_hdmi_read(offset, val);

    return val;
}

static void allwinner_a10_hdmi_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwA10HdmiState *s = AW_A10_HDMI(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_DDC_CTRL:
        if (val & FIELD_DDC_CTRL_SW_RST) {
            val &= ~FIELD_DDC_CTRL_SW_RST;
        }
        if (val & FIELD_DDC_CTRL_ACCESS_CMD_START) {
            val &= ~FIELD_DDC_CTRL_ACCESS_CMD_START;
            if (s->regs[REG_INDEX(REG_DDC_COMMAND)] == DDC_COMMAND_E_DDC_READ) {
                uint32_t regval = s->regs[REG_INDEX(REG_DDC_SLAVE_ADDRESS)];
                uint8_t segment = 0xFFu &
                    (regval >> FIELD_DDC_SLAVE_ADDRESS_SEGMENT_SHIFT);
                uint8_t offset = 0xFFu &
                    (regval >> FIELD_DDC_SLAVE_ADDRESS_OFFSET_SHIFT);
                if (segment == 0) {
                    s->edid_reg = offset;
                }
            }
        }
        break;
    case REG_DDC_INT_STATUS:
        /* Clear interrupts */
        val = s->regs[REG_INDEX(REG_DDC_INT_STATUS)] & ~(val & 0xFFu);
        /* Set transfer complete */
        val |= FIELD_DDC_INT_STATUS_TRANSFER_COMPLETE;
        break;
    case REG_DDC_FIFO_CTRL:
        if (val & FIELD_FIFO_CTRL_ADDRESS_CLEAR) {
            val &= ~FIELD_FIFO_CTRL_ADDRESS_CLEAR;
        }
        break;
    case 0x544 ... AW_A10_HDMI_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        break;
    default:
        break;
    }

    trace_allwinner_a10_hdmi_write(offset, (uint32_t)val);

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_a10_hdmi_ops = {
    .read = allwinner_a10_hdmi_read,
    .write = allwinner_a10_hdmi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl.min_access_size = 1,
};

static void allwinner_a10_hdmi_reset_enter(Object *obj, ResetType type)
{
    AwA10HdmiState *s = AW_A10_HDMI(obj);

    s->edid_reg = 0;
}

static void allwinner_a10_hdmi_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwA10HdmiState *s = AW_A10_HDMI(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_a10_hdmi_ops, s,
                          TYPE_AW_A10_HDMI, AW_A10_HDMI_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qemu_edid_generate(s->edid_blob, sizeof(s->edid_blob), &s->edid_info);
}

static const VMStateDescription allwinner_a10_hdmi_vmstate = {
    .name = "allwinner-a10-hdmi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10HdmiState, AW_A10_HDMI_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static Property allwinner_a10_hdmi_properties[] = {
    DEFINE_EDID_PROPERTIES(AwA10HdmiState, edid_info),
    DEFINE_PROP_END_OF_LIST(),
};

static void allwinner_a10_hdmi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, allwinner_a10_hdmi_properties);

    rc->phases.enter = allwinner_a10_hdmi_reset_enter;
    dc->vmsd = &allwinner_a10_hdmi_vmstate;
}

static const TypeInfo allwinner_a10_hdmi_info = {
    .name          = TYPE_AW_A10_HDMI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_hdmi_init,
    .instance_size = sizeof(AwA10HdmiState),
    .class_init    = allwinner_a10_hdmi_class_init,
};

static void allwinner_a10_hdmi_register(void)
{
    type_register_static(&allwinner_a10_hdmi_info);
}

type_init(allwinner_a10_hdmi_register)
