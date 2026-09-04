/*
 * Microchip PolarFire SoC IOSCB module emulation
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
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/misc/mchp_pfsoc_ioscb.h"

/*
 * The whole IOSCB module registers map into the system address at 0x3000_0000,
 * named as "System Port 0 (AXI-D0)".
 */
#define IOSCB_WHOLE_REG_SIZE        0x10000000
#define IOSCB_SUBMOD_REG_SIZE       0x1000
#define IOSCB_CCC_REG_SIZE          0x2000000
#define IOSCB_CTRL_REG_SIZE         0x800
#define IOSCB_QSPIXIP_REG_SIZE      0x200
#define IOSCB_SERIAL_NUMBER_SIZE    16U
#define IOSCB_DEFAULT_SERIAL_NUMBER "0123456789abcdef"
#define IOSCB_PROP_SERIAL_NUMBER    "serial-number"


/*
 * There are many sub-modules in the IOSCB module.
 * See Microchip PolarFire SoC documentation (Register_Map.zip),
 * Register Map/PF_SoC_RegMap_V1_1/MPFS250T/mpfs250t_ioscb_memmap_dri.htm
 *
 * The following are sub-modules offsets that are of concern.
 */
#define IOSCB_LANE01_BASE           0x06500000
#define IOSCB_LANE23_BASE           0x06510000
#define IOSCB_CTRL_BASE             0x07020000
#define IOSCB_QSPIXIP_BASE          0x07020100
#define IOSCB_MAILBOX_BASE          0x07020800
#define IOSCB_CFG_BASE              0x07080000
#define IOSCB_CCC_BASE              0x08000000
#define IOSCB_PLL_NW0_BASE          0x08100000
#define IOSCB_PLL_NW1_BASE          0x08200000
#define IOSCB_PLL_MSS_BASE          0x0E001000
#define IOSCB_CFM_MSS_BASE          0x0E002000
#define IOSCB_PLL_DDR_BASE          0x0E010000
#define IOSCB_BC_DDR_BASE           0x0E020000
#define IOSCB_IO_CALIB_DDR_BASE     0x0E040000
#define IOSCB_PLL_SGMII_BASE        0x0E080000
#define IOSCB_DLL_SGMII_BASE        0x0E100000
#define IOSCB_CFM_SGMII_BASE        0x0E200000
#define IOSCB_BC_SGMII_BASE         0x0E400000
#define IOSCB_IO_CALIB_SGMII_BASE   0x0E800000

static uint64_t mchp_pfsoc_dummy_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                  "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, offset);

    return 0;
}

static void mchp_pfsoc_dummy_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %d, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_dummy_ops = {
    .read = mchp_pfsoc_dummy_read,
    .write = mchp_pfsoc_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* All PLL modules in IOSCB have the same register layout */

#define PLL_CTRL    0x04
#define PLL_REF_FB  0x08
#define PLL_DIV_0_1 0x10
#define PLL_DIV_2_3 0x14
#define PLL_CTRL2   0x18
#define PLL_CAL     0x1c
#define PLL_PHADJ   0x20
#define SSCG_REG_0  0x24
#define SSCG_REG_1  0x28
#define SSCG_REG_2  0x2c
#define SSCG_REG_3  0x30

static uint64_t mchp_pfsoc_pll_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case PLL_CTRL:
        /* PLL is locked */
        val = BIT(25);
        break;
    case PLL_DIV_0_1:
    case PLL_DIV_2_3:
        val = 0x01000100; /* return valid post divider values */
        break;
    case PLL_CTRL2:
        val = 0x00001110;
        break;
    case PLL_REF_FB:
        val = 0x00000100; /* RFDIV := 1 */
        break;
    case SSCG_REG_2:
        val = 0x00000001; /* INTIN := 1 */
        break;
    case PLL_PHADJ:
        val = 0x00000401;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_pll_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                       "(size %d, value 0x%" PRIx64
                       ", offset 0x%" HWADDR_PRIx ")\n",
                       __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_pll_ops = {
    .read = mchp_pfsoc_pll_read,
    .write = mchp_pfsoc_pll_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* IO_CALIB_DDR submodule */

#define IO_CALIB_DDR_IOC_REG1   0x08

static uint64_t mchp_pfsoc_io_calib_ddr_read(void *opaque, hwaddr offset,
                                             unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case IO_CALIB_DDR_IOC_REG1:
        /* calibration completed */
        val = BIT(2);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static const MemoryRegionOps mchp_pfsoc_io_calib_ddr_ops = {
    .read = mchp_pfsoc_io_calib_ddr_read,
    .write = mchp_pfsoc_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define SERVICES_CR                         0x50
#define SERVICES_CR_REQUEST                 BIT(0)
#define SERVICES_CR_NOTIFY                  BIT(3)
#define SERVICES_CR_COMMAND_SHIFT           16
#define SERVICES_CR_COMMAND_WIDTH           8
#define SERVICES_CR_COMMAND_MASK            \
        MAKE_64BIT_MASK(SERVICES_CR_COMMAND_SHIFT, SERVICES_CR_COMMAND_WIDTH)
#define SERVICES_CR_MASK                    \
        (SERVICES_CR_REQUEST | SERVICES_CR_NOTIFY | SERVICES_CR_COMMAND_MASK)
#define SERVICES_SR                         0x54
#define SERVICES_SR_STATUS_SHIFT            16
#define SERVICES_COMMAND_SERIAL_NUMBER      0
#define SERVICES_STATUS_SUCCESS             0
#define SERVICES_STATUS_FAILED              1
#define SERVICES_MAILBOX_RESPONSE_OFFSET    0

static void mchp_pfsoc_ioscb_update_irq(MchpPfSoCIoscbState *s)
{
    qemu_set_irq(s->irq, s->irq_pending);
}

static void mchp_pfsoc_ioscb_clear_irq(void *opaque, int n, int level)
{
    MchpPfSoCIoscbState *s = opaque;

    if (level) {
        s->irq_pending = false;
        mchp_pfsoc_ioscb_update_irq(s);
    }
}

static void services_cr_write(MchpPfSoCIoscbState *s, uint32_t value)
{
    uint32_t command;
    uint32_t status = SERVICES_STATUS_FAILED;

    if (device_is_in_reset(DEVICE(s)) ||
        !(value & SERVICES_CR_REQUEST)) {
        return;
    }

    /*
     * System services complete synchronously in this model, so clear the
     * request bit before exposing the response to the guest.
     */
    s->services_cr &= ~SERVICES_CR_REQUEST;

    command = (value & SERVICES_CR_COMMAND_MASK) >>
              SERVICES_CR_COMMAND_SHIFT;
    if (command == SERVICES_COMMAND_SERIAL_NUMBER) {
        /*
         * The serial-number service returns a 128-bit response starting at
         * the beginning of the mailbox.
         */
        memset(&s->mailbox_data[SERVICES_MAILBOX_RESPONSE_OFFSET], 0,
               IOSCB_SERIAL_NUMBER_SIZE);
        memcpy(&s->mailbox_data[SERVICES_MAILBOX_RESPONSE_OFFSET],
               s->serial_number, strlen(s->serial_number));
        status = SERVICES_STATUS_SUCCESS;
    }

    s->services_sr = status << SERVICES_SR_STATUS_SHIFT;
    /*
     * HSS and U-Boot submit polling requests with REQUEST set and NOTIFY
     * clear, then poll REQUEST/BUSY for completion. Linux sets both bits
     * and expects completion through PLIC source 96.
     */
    if (value & SERVICES_CR_NOTIFY) {
        s->irq_pending = true;
        mchp_pfsoc_ioscb_update_irq(s);
    }
}

static uint64_t mchp_pfsoc_ctrl_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    MchpPfSoCIoscbState *s = opaque;

    switch (offset) {
    case SERVICES_CR:
        return s->services_cr;
    case SERVICES_SR:
        return s->services_sr;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        return 0;
    }
}

static void mchp_pfsoc_ctrl_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    MchpPfSoCIoscbState *s = opaque;

    switch (offset) {
    case SERVICES_CR:
        s->services_cr = value & SERVICES_CR_MASK;
        services_cr_write(s, value);
        break;
    case SERVICES_SR:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                      "(size %d, value 0x%" PRIx64
                      ", offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, value, offset);
        break;
    }
}

static const MemoryRegionOps mchp_pfsoc_ctrl_ops = {
    .read = mchp_pfsoc_ctrl_read,
    .write = mchp_pfsoc_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = sizeof(uint32_t),
        .max_access_size = sizeof(uint32_t),
    },
};

/*
 * The System Controller uses the mailbox as a byte-addressable shared buffer
 * for service command payloads and responses.
 */
static uint64_t mchp_pfsoc_mailbox_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    MchpPfSoCIoscbState *s = opaque;

    return ldn_le_p(&s->mailbox_data[offset], size);
}

static void mchp_pfsoc_mailbox_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    MchpPfSoCIoscbState *s = opaque;

    stn_le_p(&s->mailbox_data[offset], size, value);
}

static const MemoryRegionOps mchp_pfsoc_mailbox_ops = {
    .read = mchp_pfsoc_mailbox_read,
    .write = mchp_pfsoc_mailbox_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = sizeof(uint8_t),
        .max_access_size = sizeof(uint32_t),
    },
};

static void mchp_pfsoc_ioscb_reset(DeviceState *dev)
{
    MchpPfSoCIoscbState *s = MCHP_PFSOC_IOSCB(dev);

    s->services_cr = 0;
    s->services_sr = 0;
    memset(s->mailbox_data, 0, sizeof(s->mailbox_data));
    s->irq_pending = false;
    mchp_pfsoc_ioscb_update_irq(s);
}

static const Property mchp_pfsoc_ioscb_properties[] = {
    DEFINE_PROP_STRING(IOSCB_PROP_SERIAL_NUMBER,
                       MchpPfSoCIoscbState, serial_number),
};

static void mchp_pfsoc_ioscb_init(Object *obj)
{
    /* Accept service interrupt acknowledgements from SYSREG MESSAGE_INT */
    qdev_init_gpio_in_named(DEVICE(obj), mchp_pfsoc_ioscb_clear_irq,
                            MCHP_PFSOC_IOSCB_IRQ_CLEAR, 1);
}

static void mchp_pfsoc_ioscb_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCIoscbState *s = MCHP_PFSOC_IOSCB(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Use a deterministic identity when no serial number is configured */
    if (!s->serial_number) {
        s->serial_number = g_strdup(IOSCB_DEFAULT_SERIAL_NUMBER);
    }
    if (strlen(s->serial_number) > IOSCB_SERIAL_NUMBER_SIZE) {
        error_setg(errp, "The serial number can't be longer than %u bytes",
                   IOSCB_SERIAL_NUMBER_SIZE);
        return;
    }

    memory_region_init(&s->container, OBJECT(s),
                       "mchp.pfsoc.ioscb", IOSCB_WHOLE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    /* add subregions for all sub-modules in IOSCB */

    memory_region_init_io(&s->lane01, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.lane01", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_LANE01_BASE, &s->lane01);

    memory_region_init_io(&s->lane23, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.lane23", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_LANE23_BASE, &s->lane23);

    memory_region_init_io(&s->ctrl, OBJECT(s), &mchp_pfsoc_ctrl_ops, s,
                          "mchp.pfsoc.ioscb.ctrl", IOSCB_CTRL_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CTRL_BASE, &s->ctrl);

    memory_region_init_io(&s->qspixip, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.qspixip", IOSCB_QSPIXIP_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_QSPIXIP_BASE, &s->qspixip);

    memory_region_init_io(&s->mailbox, OBJECT(s), &mchp_pfsoc_mailbox_ops, s,
                          "mchp.pfsoc.ioscb.mailbox", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_MAILBOX_BASE, &s->mailbox);

    memory_region_init_io(&s->cfg, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.cfg", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CFG_BASE, &s->cfg);

    memory_region_init_io(&s->ccc, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.ccc", IOSCB_CCC_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CCC_BASE, &s->ccc);

    memory_region_init_io(&s->pll_nw_0, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_nw_0", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_NW0_BASE, &s->pll_nw_0);

    memory_region_init_io(&s->pll_nw_1, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_nw_1", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_NW1_BASE, &s->pll_nw_1);

    memory_region_init_io(&s->pll_mss, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_mss", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_MSS_BASE, &s->pll_mss);

    memory_region_init_io(&s->cfm_mss, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.cfm_mss", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CFM_MSS_BASE, &s->cfm_mss);

    memory_region_init_io(&s->pll_ddr, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_ddr", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_DDR_BASE, &s->pll_ddr);

    memory_region_init_io(&s->bc_ddr, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.bc_ddr", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_BC_DDR_BASE, &s->bc_ddr);

    memory_region_init_io(&s->io_calib_ddr, OBJECT(s),
                          &mchp_pfsoc_io_calib_ddr_ops, s,
                          "mchp.pfsoc.ioscb.io_calib_ddr",
                          IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_IO_CALIB_DDR_BASE,
                                &s->io_calib_ddr);

    memory_region_init_io(&s->pll_sgmii, OBJECT(s), &mchp_pfsoc_pll_ops, s,
                          "mchp.pfsoc.ioscb.pll_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_PLL_SGMII_BASE,
                                &s->pll_sgmii);

    memory_region_init_io(&s->dll_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.dll_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_DLL_SGMII_BASE,
                                &s->dll_sgmii);

    memory_region_init_io(&s->cfm_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.cfm_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_CFM_SGMII_BASE,
                                &s->cfm_sgmii);

    memory_region_init_io(&s->bc_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops, s,
                          "mchp.pfsoc.ioscb.bc_sgmii", IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_BC_SGMII_BASE,
                                &s->bc_sgmii);

    memory_region_init_io(&s->io_calib_sgmii, OBJECT(s), &mchp_pfsoc_dummy_ops,
                          s, "mchp.pfsoc.ioscb.io_calib_sgmii",
                          IOSCB_SUBMOD_REG_SIZE);
    memory_region_add_subregion(&s->container, IOSCB_IO_CALIB_SGMII_BASE,
                                &s->io_calib_sgmii);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void mchp_pfsoc_ioscb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC IOSCB modules";
    dc->realize = mchp_pfsoc_ioscb_realize;
    device_class_set_legacy_reset(dc, mchp_pfsoc_ioscb_reset);
    device_class_set_props(dc, mchp_pfsoc_ioscb_properties);
}

static const TypeInfo mchp_pfsoc_ioscb_info = {
    .name          = TYPE_MCHP_PFSOC_IOSCB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCIoscbState),
    .instance_init = mchp_pfsoc_ioscb_init,
    .class_init    = mchp_pfsoc_ioscb_class_init,
};

static void mchp_pfsoc_ioscb_register_types(void)
{
    type_register_static(&mchp_pfsoc_ioscb_info);
}

type_init(mchp_pfsoc_ioscb_register_types)
