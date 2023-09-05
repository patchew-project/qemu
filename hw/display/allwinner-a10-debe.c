/*
 * Allwinner A10 Display Engine Backend emulation
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
#include "hw/qdev-properties.h"
#include "hw/display/allwinner-a10-debe.h"
#include "trace.h"

/* DEBE register offsets - only important ones */
enum {
    REG_DEBE_MODCTL         = 0x0800, /* DE mode control */
    REG_DEBE_DISSIZE        = 0x0808, /* DE display size */
    REG_DEBE_LAY0FB_L32ADD  = 0x0850, /* DE Layer 0 lower 32-bit address */
    REG_DEBE_REGBUFFCTL     = 0x0870, /* DE buffer control register */
    REG_DEBE_ATTCTL_REG1_L0 = 0x08A0, /* DE Layer 0 attribute ctrl reg 1 */
};

/* DEBE_DISSIZE fields */
#define FIELD_DEBE_DISSIZE_DIS_HEIGHT       (16)
#define FIELD_DEBE_DISSIZE_DIS_WIDTH        (0)
#define DEBE_DISSIZE_DIS_MASK               (0xFFFFu)

/* DEBE_REGBUFFCTL fields */
#define FIELD_DEBE_REGBUFFCTL_REGLOADCTL        (1)
#define FIELD_DEBE_REGBUFFCTL_REGAUTOLOAD_DIS   (2)

/* DEBE_ATTCTL_REG1_L0 fields */
#define FIELD_DEBE_ATTCTL_REG1_L0_LAY_FBFMT     (8)
#define DEBE_ATTCTL_REG1_L0_LAY_FBFMT_MASK      (0xFu)
enum {
    ATTCTL_REG1_LAY_FBFMT_MONO_1BPP = 0,
    ATTCTL_REG1_LAY_FBFMT_MONO_2BPP,
    ATTCTL_REG1_LAY_FBFMT_MONO_4BPP,
    ATTCTL_REG1_LAY_FBFMT_MONO_8BPP,
    ATTCTL_REG1_LAY_FBFMT_COLOR_16BPP_655,
    ATTCTL_REG1_LAY_FBFMT_COLOR_16BPP_565,
    ATTCTL_REG1_LAY_FBFMT_COLOR_16BPP_556,
    ATTCTL_REG1_LAY_FBFMT_COLOR_16BPP_1555,
    ATTCTL_REG1_LAY_FBFMT_COLOR_16BPP_5551,
    ATTCTL_REG1_LAY_FBFMT_COLOR_32BPP_P888,
    ATTCTL_REG1_LAY_FBFMT_COLOR_32BPP_8888,
    ATTCTL_REG1_LAY_FBFMT_COLOR_24BPP_888,
    ATTCTL_REG1_LAY_FBFMT_COLOR_16BPP_4444,
};

static uint8_t debe_lay_fbfmt_bpp[] = {
    1,
    2,
    4,
    8,
    16,
    16,
    16,
    16,
    16,
    32,
    32,
    24,
    16
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

static uint64_t allwinner_a10_debe_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    const AwA10DEBEState *s = AW_A10_DEBE(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = 0;

    switch (offset) {
    case REG_DEBE_DISSIZE:
    case REG_DEBE_LAY0FB_L32ADD:
    case REG_DEBE_REGBUFFCTL:
        break;
    case 0x5800 ... AW_A10_DEBE_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        return 0;
    default:
        break;
    }

    val = s->regs[idx];

    trace_allwinner_a10_debe_read(offset, val);

    return val;
}

static void allwinner_a10_debe_write(void *opaque, hwaddr offset,
                                     uint64_t val, unsigned size)
{
    AwA10DEBEState *s = AW_A10_DEBE(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_a10_debe_write(offset, (uint32_t)val);

    switch (offset) {
    case REG_DEBE_DISSIZE:
        /* Store display width and height */
        s->height = 1 +
            ((val >> FIELD_DEBE_DISSIZE_DIS_HEIGHT) & DEBE_DISSIZE_DIS_MASK);
        s->width = 1 +
            ((val >> FIELD_DEBE_DISSIZE_DIS_WIDTH) & DEBE_DISSIZE_DIS_MASK);
        s->invalidate = true;
        break;
    case REG_DEBE_LAY0FB_L32ADD:
        /* Store framebuffer offset */
        s->framebuffer_offset = s->ram_base + (val >> 3);
        if (val != 0) {
            s->ready = true;
        }
        break;
    case REG_DEBE_REGBUFFCTL:
        if (val ==
            (FIELD_DEBE_REGBUFFCTL_REGLOADCTL |
             FIELD_DEBE_REGBUFFCTL_REGAUTOLOAD_DIS)) {
            /* Clear to indicate that register loading is done. */
            val &= ~FIELD_DEBE_REGBUFFCTL_REGLOADCTL;
        }
        break;
    case REG_DEBE_ATTCTL_REG1_L0:
        {
            uint8_t bpp = (val >> FIELD_DEBE_ATTCTL_REG1_L0_LAY_FBFMT) &
                DEBE_ATTCTL_REG1_L0_LAY_FBFMT_MASK;
            s->bpp = debe_lay_fbfmt_bpp[bpp];
        }
        break;
    case 0x5800 ... AW_A10_DEBE_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_a10_debe_ops = {
    .read = allwinner_a10_debe_read,
    .write = allwinner_a10_debe_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_a10_debe_reset_enter(Object *obj, ResetType type)
{
    AwA10DEBEState *s = AW_A10_DEBE(obj);

    memset(&s->regs[0], 0, AW_A10_DEBE_IOSIZE);
}

static void allwinner_a10_debe_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwA10DEBEState *s = AW_A10_DEBE(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_a10_debe_ops, s,
                          TYPE_AW_A10_DEBE, AW_A10_DEBE_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_a10_debe_vmstate = {
    .name = "allwinner-a10-debe",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10DEBEState, AW_A10_DEBE_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static Property allwinner_a10_debe_properties[] = {
    DEFINE_PROP_UINT64("ram-base", AwA10DEBEState, ram_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void allwinner_a10_debe_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = allwinner_a10_debe_reset_enter;
    dc->vmsd = &allwinner_a10_debe_vmstate;
    device_class_set_props(dc, allwinner_a10_debe_properties);
}

static const TypeInfo allwinner_a10_debe_info = {
    .name          = TYPE_AW_A10_DEBE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_debe_init,
    .instance_size = sizeof(AwA10DEBEState),
    .class_init    = allwinner_a10_debe_class_init,
};

static void allwinner_a10_debe_register(void)
{
    type_register_static(&allwinner_a10_debe_info);
}

type_init(allwinner_a10_debe_register)
