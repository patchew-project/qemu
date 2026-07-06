/*
 * i.MX 8M Plus System Reset Controller
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/imx8mp_src.h"
#include "hw/core/resettable.h"
#include "hw/core/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"

REG32(SRC_SCR, 0x0000)

REG32(SRC_A53RCR0, 0x0004)

REG32(SRC_A53RCR1, 0x0008)

REG32(SRC_M7RCR, 0x000c)
REG32(SRC_SUPERMIX_RCR, 0x0018)
REG32(SRC_AUDIOMIX_RCR, 0x001c)
REG32(SRC_USBPHY1_RCR, 0x0020)
REG32(SRC_USBPHY2_RCR, 0x0024)
REG32(SRC_MLMIX_RCR, 0x0028)
REG32(SRC_PCIEPHY_RCR, 0x002c)
REG32(SRC_HDMI_RCR, 0x0030)
REG32(SRC_MEDIA_RCR, 0x0034)
REG32(SRC_GPU2D_RCR, 0x0038)
REG32(SRC_GPU3D_RCR, 0x003c)
REG32(SRC_GPU_RCR, 0x0040)
REG32(SRC_VPU_RCR, 0x0044)
REG32(SRC_VPU_G1_RCR, 0x0048)
REG32(SRC_VPU_G2_RCR, 0x004c)
REG32(SRC_VPUVC8KE_RCR, 0x0050)
REG32(SRC_NOC_RCR, 0x0054)
REG32(SRC_SBMR1, 0x0058)
REG32(SRC_SRSR, 0x005c)
REG32(SRC_SISR, 0x0068)
REG32(SRC_SIMR, 0x006c)

REG32(SRC_SBMR2, 0x0070)

REG32(SRC_GPR1, 0x0074)
REG32(SRC_GPR2, 0x0078)
REG32(SRC_GPR3, 0x007c)
REG32(SRC_GPR4, 0x0080)
REG32(SRC_GPR5, 0x0084)
REG32(SRC_GPR6, 0x0088)
REG32(SRC_GPR7, 0x008c)
REG32(SRC_GPR8, 0x0090)
REG32(SRC_GPR9, 0x0094)
REG32(SRC_GPR10, 0x0098)
REG32(SRC_DDRC_RCR, 0x1000)
REG32(SRC_HDMIPHY_RCR, 0x1008)
REG32(SRC_MIPIPHY1_RCR, 0x100c)
REG32(SRC_MIPIPHY2_RCR, 0x1010)
REG32(SRC_HSIO_RCR, 0x1014)
REG32(SRC_MEDIAISPDWP_RCR, 0x1018)

static const char *fsl_imx8mp_src_reg_name(uint32_t reg)
{
    switch (reg) {
    case R_SRC_SCR:
        return " (SRC_SCR)";
    case R_SRC_A53RCR0:
        return " (SRC_A53RCR0)";
    case R_SRC_A53RCR1:
        return " (SRC_A53RCR1)";
    case R_SRC_M7RCR:
        return " (SRC_M7RCR)";
    case R_SRC_SUPERMIX_RCR:
        return " (SRC_SUPERMIX_RCR)";
    case R_SRC_AUDIOMIX_RCR:
        return " (SRC_AUDIOMIX_RCR)";
    case R_SRC_USBPHY1_RCR:
        return " (SRC_USBPHY1_RCR)";
    case R_SRC_USBPHY2_RCR:
        return " (SRC_USBPHY2_RCR)";
    case R_SRC_MLMIX_RCR:
        return " (SRC_MLMIX_RCR)";
    case R_SRC_PCIEPHY_RCR:
        return " (SRC_PCIEPHY_RCR)";
    case R_SRC_HDMI_RCR:
        return " (SRC_HDMI_RCR)";
    case R_SRC_MEDIA_RCR:
        return " (SRC_MEDIA_RCR)";
    case R_SRC_GPU2D_RCR:
        return " (SRC_GPU2D_RCR)";
    case R_SRC_GPU3D_RCR:
        return " (SRC_GPU3D_RCR)";
    case R_SRC_GPU_RCR:
        return " (SRC_GPU_RCR)";
    case R_SRC_VPU_RCR:
        return " (SRC_VPU_RCR)";
    case R_SRC_VPU_G1_RCR:
        return " (SRC_VPU_G1_RCR)";
    case R_SRC_VPU_G2_RCR:
        return " (SRC_VPU_G2_RCR)";
    case R_SRC_VPUVC8KE_RCR:
        return " (SRC_VPUVC8KE_RCR)";
    case R_SRC_NOC_RCR:
        return " (SRC_NOC_RCR)";
    case R_SRC_SBMR1:
        return " (SRC_SBMR1)";
    case R_SRC_SRSR:
        return " (SRC_SRSR)";
    case R_SRC_SISR:
        return " (SRC_SISR)";
    case R_SRC_SIMR:
        return " (SRC_SIMR)";
    case R_SRC_SBMR2:
        return " (SRC_SBMR2)";
    case R_SRC_GPR1:
        return " (SRC_GPR1)";
    case R_SRC_GPR2:
        return " (SRC_GPR2)";
    case R_SRC_GPR3:
        return " (SRC_GPR3)";
    case R_SRC_GPR4:
        return " (SRC_GPR4)";
    case R_SRC_GPR5:
        return " (SRC_GPR5)";
    case R_SRC_GPR6:
        return " (SRC_GPR6)";
    case R_SRC_GPR7:
        return " (SRC_GPR7)";
    case R_SRC_GPR8:
        return " (SRC_GPR8)";
    case R_SRC_GPR9:
        return " (SRC_GPR9)";
    case R_SRC_GPR10:
        return " (SRC_GPR10)";
    case R_SRC_DDRC_RCR:
        return " (SRC_DDRC_RCR)";
    case R_SRC_HDMIPHY_RCR:
        return " (SRC_HDMIPHY_RCR)";
    case R_SRC_MIPIPHY1_RCR:
        return " (SRC_MIPIPHY1_RCR)";
    case R_SRC_MIPIPHY2_RCR:
        return " (SRC_MIPIPHY2_RCR)";
    case R_SRC_HSIO_RCR:
        return " (SRC_HSIO_RCR)";
    case R_SRC_MEDIAISPDWP_RCR:
        return " (SRC_MEDIAISPDWP_RCR)";
    default:
        return " (reserved)";
    }
}

static uint64_t fsl_imx8mp_src_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    FslImx8mpSrcState *s = opaque;
    const uint32_t reg = offset / 4;
    uint32_t value = 0;

    switch (reg) {
    default:
        if (reg < FSL_IMX8MP_SRC_NUM_REGS) {
            value = s->regs[reg];
        }
        qemu_log_mask(LOG_UNIMP, "[%s]%s: Unimplemented register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX8MP_SRC, __func__,
                      offset);
        break;
    }

    trace_fsl_imx8mp_src_read(offset, fsl_imx8mp_src_reg_name(reg), value);

    return value;
}

static ResettablePhases imx8mp_src_parent_phases;

static void imx8mp_src_reset_enter(Object *obj, ResetType type)
{
    FslImx8mpSrcState *s = IMX8MP_SRC(obj);

    if (imx8mp_src_parent_phases.enter) {
        imx8mp_src_parent_phases.enter(obj, type);
    }

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * Default CM7 STOP state for Linux imx-rproc MMIO mode detection.
    */
    s->regs[R_SRC_M7RCR] = 0x000000A8;
}

static void fsl_imx8mp_src_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    FslImx8mpSrcState *s = opaque;
    const uint32_t reg = offset / 4;

    switch (reg) {
    default:
        if (reg < FSL_IMX8MP_SRC_NUM_REGS) {
            s->regs[reg] = value;
        }
        qemu_log_mask(LOG_UNIMP, "[%s]%s: Unimplemented register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX8MP_SRC, __func__,
                      offset);
        break;
    }

    trace_fsl_imx8mp_src_write(offset, fsl_imx8mp_src_reg_name(reg), value);
}

static const struct MemoryRegionOps imx8mp_src_ops = {
    .read = fsl_imx8mp_src_read,
    .write = fsl_imx8mp_src_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx8mp_src_realize(DeviceState *dev, Error **errp)
{
    FslImx8mpSrcState *s = IMX8MP_SRC(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx8mp_src_ops, s,
                          TYPE_IMX8MP_SRC, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static const VMStateDescription imx8mp_src_vmstate = {
    .name = TYPE_IMX8MP_SRC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FslImx8mpSrcState, FSL_IMX8MP_SRC_NUM_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static void imx8mp_src_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc,
                                       imx8mp_src_reset_enter,
                                       NULL,
                                       NULL,
                                       &imx8mp_src_parent_phases);
    dc->realize = imx8mp_src_realize;
    dc->vmsd = &imx8mp_src_vmstate;
    dc->desc = "i.MX 8M Plus System Reset Controller";
}

static const TypeInfo imx8mp_src_types[] = {
    {
        .name          = TYPE_IMX8MP_SRC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslImx8mpSrcState),
        .class_init    = imx8mp_src_class_init,
    },
};

DEFINE_TYPES(imx8mp_src_types)
