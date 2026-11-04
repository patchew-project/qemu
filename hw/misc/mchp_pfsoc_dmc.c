/*
 * Microchip PolarFire SoC DDR Memory Controller module emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * Updated by Bin Meng <bin.meng@processmission.com> to support DDR
 * training emulation for with newer version Hart Software Services,
 * aka HSS.
 *
 * The DMC register model is based on HSS v2024.06. Its read/write
 * behavior is the minimum needed for DDR training to complete and
 * may not reflect actual hardware register behavior.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/misc/mchp_pfsoc_dmc.h"

/* DDR SGMII PHY module */

#define PHY_ADDCMD_CK_TRANSITION_TAP              12
#define PHY_ADDCMD_A5_TRANSITION_TAP              20
#define PHY_ADDCMD_CK_READBACK                    5
#define PHY_ADDCMD_A5_READBACK                    (3 << 8)
#define PHY_GT_TXDLY_VALUE                        0x01010101
#define PHY_DQDQS_STATUS2_VALUE                   5

#define SGMII_PHY_PLL_CTRL_MAIN                   0x084
#define SGMII_PHY_PLL_CTRL_MAIN_CONTROL_LO_MASK   0x0000007f
#define SGMII_PHY_PLL_CTRL_MAIN_LP_REQUIRES_LOCK  BIT(24)
#define SGMII_PHY_PLL_CTRL_MAIN_LOCK              BIT(25)
#define SGMII_PHY_IOC_REG1                        0x208
#define SGMII_PHY_TRAINING_STATUS                 0x814
#define SGMII_PHY_GT_ERR_COMB                     0x81c
#define SGMII_PHY_GT_CLK_SEL                      0x820
#define SGMII_PHY_GT_TXDLY                        0x824
#define SGMII_PHY_DQ_DQS_ERR_DONE                 0x834
#define SGMII_PHY_DQDQS_STATUS1                   0x84c
#define SGMII_PHY_DQDQS_STATUS2                   0x850
#define SGMII_PHY_EXPERT_DLYCNT_MOVE_REG1         0x880
#define SGMII_PHY_EXPERT_DLYCNT_MOVE_CONTROL_MASK 0x001fffff
#define SGMII_PHY_EXPERT_DLYCNT_MOVE_ADDCMD_MASK  0x00180000
#define SGMII_PHY_EXPERT_DLYCNT_LOAD_REG1         0x890
#define SGMII_PHY_EXPERT_DLYCNT_LOAD_CONTROL_MASK 0x001fffff
#define SGMII_PHY_EXPERT_DLYCNT_LOAD_ADDCMD_MASK  0x00180000
#define SGMII_PHY_EXPERT_ADDCMD_LN_READBACK       0x8ac
#define SGMII_PHY_SOFT_RESET_SGMII                0xc00
#define SGMII_PHY_SGMII_MODE                      0xc04
#define SGMII_PHY_PLL_CNTL                        0xc08
#define SGMII_PHY_PLL_CNTL_LOCK                   BIT(7)
#define SGMII_PHY_CH0_CNTL                        0xc0c
#define SGMII_PHY_CH1_CNTL                        0xc10
#define SGMII_PHY_RECAL_CNTL                      0xc14
#define SGMII_PHY_RECAL_CNTL_STATUS_MASK          0xffff0000
#define SGMII_PHY_RECAL_CNTL_LOCK                 BIT(23)
#define SGMII_PHY_CLK_CNTL                        0xc18
#define SGMII_PHY_DYN_CNTL                        0xc1c
#define SGMII_PHY_PVT_STAT                        0xc20
#define SGMII_PHY_PVT_STAT_IO_ENABLE              BIT(6)
#define SGMII_PHY_PVT_STAT_CALIBRATED             BIT(14)
#define SGMII_PHY_PVT_STAT_GUEST_CTRL_MASK        0xc0000000
#define SGMII_PHY_SPARE_CNTL                      0xc24
#define SGMII_PHY_SPARE_STAT                      0xc28

static uint64_t mchp_pfsoc_ddr_sgmii_phy_read(void *opaque, hwaddr offset,
                                              unsigned size)
{
    uint32_t val = 0;
    MchpPfSoCDdrSgmiiPhyState *s = opaque;
    uint32_t index = offset / sizeof(uint32_t);

    switch (offset) {
    case SGMII_PHY_IOC_REG1:
        /* See ddr_pvt_calibration() in HSS */
        val = BIT(4) | BIT(2);
        break;
    case SGMII_PHY_TRAINING_STATUS:
        /*
         * The codes logic emulates the training status change from
         * DDR_TRAINING_IP_SM_BCLKSCLK to DDR_TRAINING_IP_SM_DQ_DQS.
         *
         * See ddr_setup() in mss_ddr.c in the HSS source codes.
         */
        val = BIT(s->training_status_bit);
        s->training_status_bit = (s->training_status_bit + 1) % 5;
        break;
    case SGMII_PHY_DQ_DQS_ERR_DONE:
        /*
         * DDR_TRAINING_IP_SM_VERIFY state in ddr_setup(),
         * check that DQ/DQS training passed without error.
         */
        val = 8;
        break;
    case SGMII_PHY_DQDQS_STATUS1:
        /*
         * DDR_TRAINING_IP_SM_VERIFY state in ddr_setup(),
         * check that DQ/DQS calculated window is above 5 taps.
         */
        val = 0xff;
        break;
    case SGMII_PHY_PVT_STAT:
        /*
         * HSS polls IO enable and calibration status, then writes the
         * calibration lock. The HSS register definitions mark bits 31:30
         * as writable controls.
         *
         * See sgmii_channel_setup() in mss_sgmii.c in HSS.
         */
        val = s->regs[index] | SGMII_PHY_PVT_STAT_IO_ENABLE |
              SGMII_PHY_PVT_STAT_CALIBRATED;
        break;
    case SGMII_PHY_PLL_CTRL_MAIN:
        /*
         * HSS programs the main DDR PLL controls and polls LOCK after
         * changing the PLL dividers during LPDDR4 manual training. The
         * HSS register definitions mark bits 6:0 and 24 as writable.
         *
         * See ddr_pll_config() in mss_pll.c and
         * lpddr4_manual_training() in mss_ddr.c in HSS.
         */
        val = s->regs[index] | SGMII_PHY_PLL_CTRL_MAIN_LOCK;
        break;
    case SGMII_PHY_PLL_CNTL:
        /*
         * HSS programs the SGMII PLL controls and polls aro_pll0_lock.
         *
         * See setup_sgmii_rpc_per_config() and sgmii_channel_setup()
         * in mss_sgmii.c in HSS.
         */
        val = s->regs[index] | SGMII_PHY_PLL_CNTL_LOCK;
        break;
    case SGMII_PHY_RECAL_CNTL:
        /*
         * HSS programs the low control half, polls sro_dll_lock, and
         * reads sro_dll_90_code from the read-only status half.
         *
         * See setup_sgmii_rpc_per_config() and sgmii_channel_setup()
         * in mss_sgmii.c in HSS.
         */
        val = s->regs[index] | SGMII_PHY_RECAL_CNTL_LOCK;
        break;
    case SGMII_PHY_EXPERT_ADDCMD_LN_READBACK:
        /*
         * HSS pulses the ADDCMD move controls while sampling CK from
         * bits 3:0 and A5 from bits 9:8. The transition tap positions
         * below are deterministic QEMU model choices.
         *
         * See lpddr4_manual_training() in mss_ddr.c in HSS.
         */
        if (s->addcmd_tap >= PHY_ADDCMD_CK_TRANSITION_TAP) {
            val |= PHY_ADDCMD_CK_READBACK;
        }
        if (s->addcmd_tap >= PHY_ADDCMD_A5_TRANSITION_TAP) {
            val |= PHY_ADDCMD_A5_READBACK;
        }
        break;
    case SGMII_PHY_GT_ERR_COMB:
    case SGMII_PHY_GT_CLK_SEL:
        /*
         * HSS fails DDR verification on any gate error, a zero delay
         * selected by gt_clk_sel, or more than one zero delay byte.
         * Model no gate errors; the all-nonzero delay below makes the
         * deterministic clock selection immaterial.
         *
         * See DDR_TRAINING_IP_SM_VERIFY in ddr_setup() in mss_ddr.c
         * in HSS.
         */
        val = 0;
        break;
    case SGMII_PHY_GT_TXDLY:
        val = PHY_GT_TXDLY_VALUE;
        break;
    case SGMII_PHY_DQDQS_STATUS2:
        /*
         * HSS requires the calculated DQ/DQS window to be at least
         * DQ_DQS_NUM_TAPS, which is 5. Return the minimum pass value.
         *
         * See DDR_TRAINING_IP_SM_VERIFY in ddr_setup() and the
         * DQ_DQS_NUM_TAPS definition in mss_ddr.h in HSS.
         */
        val = PHY_DQDQS_STATUS2_VALUE;
        break;
    case SGMII_PHY_EXPERT_DLYCNT_MOVE_REG1:
    case SGMII_PHY_EXPERT_DLYCNT_LOAD_REG1:
    case SGMII_PHY_SOFT_RESET_SGMII:
    case SGMII_PHY_SGMII_MODE:
    case SGMII_PHY_CH0_CNTL:
    case SGMII_PHY_CH1_CNTL:
    case SGMII_PHY_CLK_CNTL:
    case SGMII_PHY_DYN_CNTL:
    case SGMII_PHY_SPARE_CNTL:
    case SGMII_PHY_SPARE_STAT:
        /*
         * HSS programs the ADDCMD and SGMII controls and reads
         * SPARE_STAT to classify the silicon and select eye-width
         * thresholds.
         *
         * See lpddr4_manual_training() in mss_ddr.c and the SGMII setup
         * functions in mss_sgmii.c in HSS.
         */
        val = s->regs[index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_ddr_sgmii_phy_write(void *opaque, hwaddr offset,
                                           uint64_t value, unsigned size)
{
    MchpPfSoCDdrSgmiiPhyState *s = opaque;
    uint32_t index = offset / sizeof(uint32_t);

    switch (offset) {
    case SGMII_PHY_IOC_REG1:
    case SGMII_PHY_TRAINING_STATUS:
    case SGMII_PHY_GT_ERR_COMB:
    case SGMII_PHY_GT_CLK_SEL:
    case SGMII_PHY_GT_TXDLY:
    case SGMII_PHY_DQ_DQS_ERR_DONE:
    case SGMII_PHY_DQDQS_STATUS1:
    case SGMII_PHY_DQDQS_STATUS2:
    case SGMII_PHY_EXPERT_ADDCMD_LN_READBACK:
    case SGMII_PHY_SPARE_STAT:
        /*
         * The HSS register definitions declare these status and
         * readback registers read-only.
         *
         * See mss_ddr_sgmii_phy_defs.h in HSS.
         */
        return;
    case SGMII_PHY_PLL_CTRL_MAIN:
        value &= SGMII_PHY_PLL_CTRL_MAIN_CONTROL_LO_MASK |
                 SGMII_PHY_PLL_CTRL_MAIN_LP_REQUIRES_LOCK;
        break;
    case SGMII_PHY_PLL_CNTL:
        value &= ~SGMII_PHY_PLL_CNTL_LOCK;
        break;
    case SGMII_PHY_RECAL_CNTL:
        value &= ~SGMII_PHY_RECAL_CNTL_STATUS_MASK;
        break;
    case SGMII_PHY_PVT_STAT:
        value &= SGMII_PHY_PVT_STAT_GUEST_CTRL_MASK;
        break;
    case SGMII_PHY_EXPERT_DLYCNT_MOVE_REG1: {
        bool active;

        /*
         * HSS pulses 0 -> 0x180000 -> 0 while scanning the ADDCMD eye.
         * Count each rising pulse as one abstract delay tap.
         *
         * See lpddr4_manual_training() in mss_ddr.c in HSS.
         */
        value &= SGMII_PHY_EXPERT_DLYCNT_MOVE_CONTROL_MASK;
        active = value & SGMII_PHY_EXPERT_DLYCNT_MOVE_ADDCMD_MASK;
        if (active && !s->addcmd_move_active &&
            s->addcmd_tap != UINT8_MAX) {
            s->addcmd_tap++;
        }
        s->addcmd_move_active = active;
        break;
    }
    case SGMII_PHY_EXPERT_DLYCNT_LOAD_REG1:
        /*
         * HSS pulses 0x180000 before each ADDCMD scan. Restart the
         * abstract QEMU tap position when that load pulse is asserted.
         *
         * See lpddr4_manual_training() in mss_ddr.c in HSS.
         */
        value &= SGMII_PHY_EXPERT_DLYCNT_LOAD_CONTROL_MASK;
        if (value & SGMII_PHY_EXPERT_DLYCNT_LOAD_ADDCMD_MASK) {
            s->addcmd_tap = 0;
            s->addcmd_move_active = false;
        }
        break;
    case SGMII_PHY_SOFT_RESET_SGMII:
    case SGMII_PHY_SGMII_MODE:
    case SGMII_PHY_CH0_CNTL:
    case SGMII_PHY_CH1_CNTL:
    case SGMII_PHY_CLK_CNTL:
    case SGMII_PHY_DYN_CNTL:
    case SGMII_PHY_SPARE_CNTL:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                      "(size %d, value 0x%" PRIx64
                      ", offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, value, offset);
        return;
    }

    s->regs[index] = value;
}

static const MemoryRegionOps mchp_pfsoc_ddr_sgmii_phy_ops = {
    .read = mchp_pfsoc_ddr_sgmii_phy_read,
    .write = mchp_pfsoc_ddr_sgmii_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_ddr_sgmii_phy_reset_hold(Object *obj, ResetType type)
{
    MchpPfSoCDdrSgmiiPhyState *s = MCHP_PFSOC_DDR_SGMII_PHY(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->training_status_bit = 0;
    s->addcmd_tap = 0;
    s->addcmd_move_active = false;
}

static void mchp_pfsoc_ddr_sgmii_phy_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCDdrSgmiiPhyState *s = MCHP_PFSOC_DDR_SGMII_PHY(dev);

    memory_region_init_io(&s->sgmii_phy, OBJECT(dev),
                          &mchp_pfsoc_ddr_sgmii_phy_ops, s,
                          "mchp.pfsoc.ddr_sgmii_phy",
                          MCHP_PFSOC_DDR_SGMII_PHY_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->sgmii_phy);
}

static void mchp_pfsoc_ddr_sgmii_phy_class_init(ObjectClass *klass,
                                                const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC DDR SGMII PHY module";
    dc->realize = mchp_pfsoc_ddr_sgmii_phy_realize;
    rc->phases.hold = mchp_pfsoc_ddr_sgmii_phy_reset_hold;
}

static const TypeInfo mchp_pfsoc_ddr_sgmii_phy_info = {
    .name          = TYPE_MCHP_PFSOC_DDR_SGMII_PHY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCDdrSgmiiPhyState),
    .class_init    = mchp_pfsoc_ddr_sgmii_phy_class_init,
};

static void mchp_pfsoc_ddr_sgmii_phy_register_types(void)
{
    type_register_static(&mchp_pfsoc_ddr_sgmii_phy_info);
}

type_init(mchp_pfsoc_ddr_sgmii_phy_register_types)

/* DDR CFG module */

#define CFG_MT_DONE_ACK                 0x4428
#define CFG_STAT_DFI_INIT_COMPLETE      0x10034
#define CFG_STAT_DFI_TRAINING_COMPLETE  0x10038

static uint64_t mchp_pfsoc_ddr_cfg_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case CFG_MT_DONE_ACK:
        /* memory test in MTC_test() */
        val = BIT(0);
        break;
    case CFG_STAT_DFI_INIT_COMPLETE:
        /* DDR_TRAINING_IP_SM_START_CHECK state in ddr_setup() */
        val = BIT(0);
        break;
    case CFG_STAT_DFI_TRAINING_COMPLETE:
        /* DDR_TRAINING_IP_SM_VERIFY state in ddr_setup() */
        val = BIT(0);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_ddr_cfg_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %d, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_ddr_cfg_ops = {
    .read = mchp_pfsoc_ddr_cfg_read,
    .write = mchp_pfsoc_ddr_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_ddr_cfg_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCDdrCfgState *s = MCHP_PFSOC_DDR_CFG(dev);

    memory_region_init_io(&s->cfg, OBJECT(dev),
                          &mchp_pfsoc_ddr_cfg_ops, s,
                          "mchp.pfsoc.ddr_cfg",
                          MCHP_PFSOC_DDR_CFG_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->cfg);
}

static void mchp_pfsoc_ddr_cfg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC DDR CFG module";
    dc->realize = mchp_pfsoc_ddr_cfg_realize;
}

static const TypeInfo mchp_pfsoc_ddr_cfg_info = {
    .name          = TYPE_MCHP_PFSOC_DDR_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCDdrCfgState),
    .class_init    = mchp_pfsoc_ddr_cfg_class_init,
};

static void mchp_pfsoc_ddr_cfg_register_types(void)
{
    type_register_static(&mchp_pfsoc_ddr_cfg_info);
}

type_init(mchp_pfsoc_ddr_cfg_register_types)
