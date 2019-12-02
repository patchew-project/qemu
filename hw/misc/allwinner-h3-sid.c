/*
 * Allwinner H3 Security ID emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
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
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/misc/allwinner-h3-sid.h"

/* SID register offsets */
#define REG_PRCTL         (0x40) /* Control */
#define REG_RDKEY         (0x60) /* Read Key */

/* SID register flags */
#define REG_PRCTL_WRITE   (0x2)  /* Unknown write flag */
#define REG_PRCTL_OP_LOCK (0xAC) /* Lock operation */

static uint64_t allwinner_h3_sid_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwH3SidState *s = (AwH3SidState *)opaque;
    uint64_t val = 0;

    switch (offset) {
    case REG_PRCTL:    /* Control */
        val = s->control;
        break;
    case REG_RDKEY:    /* Read Key */
        val = s->rdkey;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return val;
}

static void allwinner_h3_sid_write(void *opaque, hwaddr offset,
                                      uint64_t val, unsigned size)
{
    AwH3SidState *s = (AwH3SidState *)opaque;

    switch (offset) {
    case REG_PRCTL:    /* Control */
        s->control = val & ~(REG_PRCTL_WRITE);
        if (!(s->control & REG_PRCTL_OP_LOCK)) {
            uint32_t id = (s->control >> 16) / sizeof(uint32_t);
            if (id < AW_H3_SID_NUM_IDS) {
                s->rdkey = s->identifier[id];
            }
        }
        break;
    case REG_RDKEY:    /* Read Key */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }
}

static const MemoryRegionOps allwinner_h3_sid_ops = {
    .read = allwinner_h3_sid_read,
    .write = allwinner_h3_sid_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    }
};

static void allwinner_h3_sid_reset(DeviceState *dev)
{
    AwH3SidState *s = AW_H3_SID(dev);
    Error *err = NULL;

    /* Set default values for registers */
    s->control = 0;
    s->rdkey = 0;

    /* Initialize identifier data */
    for (int i = 0; i < AW_H3_SID_NUM_IDS; i++) {
        s->identifier[i] = 0;
    }

    if (qemu_guest_getrandom(s->identifier, sizeof(s->identifier), &err)) {
        error_report_err(err);
    }
}

static void allwinner_h3_sid_realize(DeviceState *dev, Error **errp)
{
}

static void allwinner_h3_sid_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwH3SidState *s = AW_H3_SID(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_h3_sid_ops, s,
                          TYPE_AW_H3_SID, AW_H3_SID_REGS_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_h3_sid_vmstate = {
    .name = TYPE_AW_H3_SID,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, AwH3SidState),
        VMSTATE_UINT32(rdkey, AwH3SidState),
        VMSTATE_UINT32_ARRAY(identifier, AwH3SidState, AW_H3_SID_NUM_IDS),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_h3_sid_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_h3_sid_reset;
    dc->realize = allwinner_h3_sid_realize;
    dc->vmsd = &allwinner_h3_sid_vmstate;
}

static const TypeInfo allwinner_h3_sid_info = {
    .name          = TYPE_AW_H3_SID,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_h3_sid_init,
    .instance_size = sizeof(AwH3SidState),
    .class_init    = allwinner_h3_sid_class_init,
};

static void allwinner_h3_sid_register(void)
{
    type_register_static(&allwinner_h3_sid_info);
}

type_init(allwinner_h3_sid_register)
