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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/msf2-sysreg.h"

#ifndef MSF2_SYSREG_ERR_DEBUG
#define MSF2_SYSREG_ERR_DEBUG  0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (MSF2_SYSREG_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt "\n", __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static inline int msf2_divbits(uint32_t div)
{
    int ret = 0;

    switch (div) {
    case 1:
        ret = 0;
        break;
    case 2:
        ret = 1;
        break;
    case 4:
        ret = 2;
        break;
    case 8:
        ret = 4;
        break;
    case 16:
        ret = 5;
        break;
    case 32:
        ret = 6;
        break;
    default:
        break;
    }

    return ret;
}

static void msf2_sysreg_reset(DeviceState *d)
{
    MSF2SysregState *s = MSF2_SYSREG(d);

    DB_PRINT("RESET");

    s->regs[MSSDDR_PLL_STATUS_LOW_CR] = 0x021A2358;
    s->regs[MSSDDR_PLL_STATUS] = 0x3;
    s->regs[MSSDDR_FACC1_CR] = msf2_divbits(s->apb0div) << 5 |
                               msf2_divbits(s->apb1div) << 2;
}

static uint64_t msf2_sysreg_read(void *opaque, hwaddr offset,
    unsigned size)
{
    MSF2SysregState *s = opaque;
    uint32_t ret = 0;

    offset >>= 2;
    if (offset < ARRAY_SIZE(s->regs)) {
        ret = s->regs[offset];
        DB_PRINT("addr: 0x%08" HWADDR_PRIx " data: 0x%08" PRIx32,
                    offset << 2, ret);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: Bad offset 0x%08" HWADDR_PRIx "\n", __func__,
                    offset << 2);
    }

    return ret;
}

static void msf2_sysreg_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size)
{
    MSF2SysregState *s = (MSF2SysregState *)opaque;
    uint32_t newval = val;
    uint32_t oldval;

    DB_PRINT("addr: 0x%08" HWADDR_PRIx " data: 0x%08" PRIx64,
            offset, val);

    offset >>= 2;

    switch (offset) {
    case MSSDDR_PLL_STATUS:
        break;

    case ESRAM_CR:
        oldval = s->regs[ESRAM_CR];
        if (oldval ^ newval) {
            qemu_log_mask(LOG_GUEST_ERROR,
                       TYPE_MSF2_SYSREG": eSRAM remapping not supported\n");
        }
        break;

    case DDR_CR:
        oldval = s->regs[DDR_CR];
        if (oldval ^ newval) {
            qemu_log_mask(LOG_GUEST_ERROR,
                       TYPE_MSF2_SYSREG": DDR remapping not supported\n");
        }
        break;

    case ENVM_REMAP_BASE_CR:
        oldval = s->regs[ENVM_REMAP_BASE_CR];
        if (oldval ^ newval) {
            qemu_log_mask(LOG_GUEST_ERROR,
                       TYPE_MSF2_SYSREG": eNVM remapping not supported\n");
        }
        break;

    default:
        if (offset < ARRAY_SIZE(s->regs)) {
            s->regs[offset] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: Bad offset 0x%08" HWADDR_PRIx "\n", __func__,
                        offset << 2);
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
        VMSTATE_UINT32_ARRAY(regs, MSF2SysregState, MSF2_SYSREG_MMIO_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static Property msf2_sysreg_properties[] = {
    /* default divisors in Libero GUI */
    DEFINE_PROP_UINT32("apb0divisor", MSF2SysregState, apb0div, 2),
    DEFINE_PROP_UINT32("apb1divisor", MSF2SysregState, apb1div, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void msf2_sysreg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_msf2_sysreg;
    dc->reset = msf2_sysreg_reset;
    dc->props = msf2_sysreg_properties;
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
