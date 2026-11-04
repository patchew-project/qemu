/*
 * Microchip PolarFire SoC SYSREG module emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
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
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/misc/mchp_pfsoc_sysreg.h"
#include "system/runstate.h"

#define CLOCK_CONFIG_CR     0x8
#define RTC_CLOCK_CR        0xc
#define MSS_RESET_CR        0x18
#define ENVM_CR             0xb8
#define MESSAGE_INT         0x118c
#define MESSAGE_INT_PENDING BIT(0)

static uint64_t mchp_pfsoc_sysreg_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    MchpPfSoCSysregState *s = opaque;
    uint32_t val = 0;

    switch (offset) {
    case CLOCK_CONFIG_CR:
        /* Icicle kit reference design cpu/axi/ahb divider setting */
        val = 0x24;
        break;
    case RTC_CLOCK_CR:
        /*
         * Bit 16 enables the RTC clock, 0x7d is the required divider
         * setting for a 125 MHz reference.
         */
        val = BIT(16) | 0x7d;
        break;
    case ENVM_CR:
        /* Indicate the eNVM is running at the configured divider rate */
        val = BIT(6);
        break;
    case MESSAGE_INT:
        val = mchp_pfsoc_ioscb_get_irq_pending(s->ioscb) ?
              MESSAGE_INT_PENDING : 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_sysreg_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    MchpPfSoCSysregState *s = opaque;
    switch (offset) {
    case MSS_RESET_CR:
        if (value == 0xdead) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    case MESSAGE_INT:
        /*
         * MESSAGE_INT bit 0 is read/write: writing zero clears the interrupt
         * and writing one sets it. IOSCB owns the state and PLIC output.
         */
        mchp_pfsoc_ioscb_set_irq_pending(s->ioscb,
                                        value & MESSAGE_INT_PENDING);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                      "(size %d, value 0x%" PRIx64
                      ", offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, value, offset);
    }
}

static const MemoryRegionOps mchp_pfsoc_sysreg_ops = {
    .read = mchp_pfsoc_sysreg_read,
    .write = mchp_pfsoc_sysreg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_sysreg_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCSysregState *s = MCHP_PFSOC_SYSREG(dev);

    if (!s->ioscb) {
        error_setg(errp, "The 'ioscb' link must be set");
        return;
    }

    memory_region_init_io(&s->sysreg, OBJECT(dev),
                          &mchp_pfsoc_sysreg_ops, s,
                          "mchp.pfsoc.sysreg",
                          MCHP_PFSOC_SYSREG_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->sysreg);
}

static const Property mchp_pfsoc_sysreg_properties[] = {
    DEFINE_PROP_LINK("ioscb", MchpPfSoCSysregState, ioscb,
                     TYPE_MCHP_PFSOC_IOSCB, MchpPfSoCIoscbState *),
};

static void mchp_pfsoc_sysreg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC SYSREG module";
    dc->realize = mchp_pfsoc_sysreg_realize;
    device_class_set_props(dc, mchp_pfsoc_sysreg_properties);
}

static const TypeInfo mchp_pfsoc_sysreg_info = {
    .name          = TYPE_MCHP_PFSOC_SYSREG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCSysregState),
    .class_init    = mchp_pfsoc_sysreg_class_init,
};

static void mchp_pfsoc_sysreg_register_types(void)
{
    type_register_static(&mchp_pfsoc_sysreg_info);
}

type_init(mchp_pfsoc_sysreg_register_types)
