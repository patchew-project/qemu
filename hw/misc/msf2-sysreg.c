/*
 * System Register block model of Microsemi SmartFusion2.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/misc/msf2-sysreg.h"

#ifndef MSF2_SYSREG_ERR_DEBUG
#define MSF2_SYSREG_ERR_DEBUG  0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (MSF2_SYSREG_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void msf2_sysreg_reset(DeviceState *d)
{
    MSF2SysregState *s = MSF2_SYSREG(d);

    DB_PRINT("RESET\n");

    s->regs[MSSDDR_PLL_STATUS_LOW_CR] = 0x02420041;
    s->regs[MSSDDR_FACC1_CR] = 0x0A482124;
    s->regs[MSSDDR_PLL_STATUS] = 0x3;
}

static uint64_t msf2_sysreg_read(void *opaque, hwaddr offset,
    unsigned size)
{
    MSF2SysregState *s = opaque;
    offset /= 4;
    uint32_t ret = 0;

    if (offset < ARRAY_SIZE(s->regs)) {
        ret = s->regs[offset];
        DB_PRINT("addr: 0x%08" HWADDR_PRIx " data: 0x%08" PRIx32 "\n",
                    offset * 4, ret);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: Bad offset 0x%08" HWADDR_PRIx "\n", __func__,
                    offset * 4);
    }

    return ret;
}

static void msf2_sysreg_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size)
{
    MSF2SysregState *s = (MSF2SysregState *)opaque;
    offset /= 4;

    DB_PRINT("addr: 0x%08" HWADDR_PRIx " data: 0x%08" PRIx64 "\n",
            offset * 4, val);

    switch (offset) {
    case MSSDDR_PLL_STATUS:
        break;

    default:
        if (offset < ARRAY_SIZE(s->regs)) {
            s->regs[offset] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: Bad offset 0x%08" HWADDR_PRIx "\n", __func__,
                        offset * 4);
        }
        break;
    }
}

static const MemoryRegionOps sysreg_ops = {
    .read = msf2_sysreg_read,
    .write = msf2_sysreg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void msf2_sysreg_init(Object *obj)
{
    MSF2SysregState *s = MSF2_SYSREG(obj);

    memory_region_init_io(&s->iomem, obj, &sysreg_ops, s, TYPE_MSF2_SYSREG,
                          MSF2_SYSREG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_msf2_sysreg = {
    .name = TYPE_MSF2_SYSREG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MSF2SysregState, MSF2_SYSREG_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void msf2_sysreg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_msf2_sysreg;
    dc->reset = msf2_sysreg_reset;
}

static const TypeInfo msf2_sysreg_info = {
    .name  = TYPE_MSF2_SYSREG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = msf2_sysreg_class_init,
    .instance_size  = sizeof(MSF2SysregState),
    .instance_init = msf2_sysreg_init,
};

static void msf2_sysreg_register_types(void)
{
    type_register_static(&msf2_sysreg_info);
}

type_init(msf2_sysreg_register_types)
