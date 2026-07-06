/*
 * i.MX 8M Plus IOMUXC GPR
 *
 * Copyright (c) 2026, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>

 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/imx8mp_gpr.h"
#include "hw/core/irq.h"
#include "hw/core/resettable.h"
#include "migration/vmstate.h"

#define IMX8MP_GPR22_OFF     0x58
#define IMX8MP_GPR22_CM7_CPUWAIT_BIT  (1u << 0)

static inline void imx8mp_gpr_update_cm7_run(IMX8MPGPRState *s, uint32_t gpr22)
{
    /* CPUWAIT=0 => run, CPUWAIT=1 => stop */
    bool run = ((gpr22 & IMX8MP_GPR22_CM7_CPUWAIT_BIT) == 0);
    qemu_set_irq(s->cm7_run_irq, run);
}

static uint64_t imx8mp_gpr_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX8MPGPRState *s = opaque;

    return s->gpr[offset >> 2];
}

static void imx8mp_gpr_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    IMX8MPGPRState *s = opaque;

    s->gpr[offset >> 2] = (uint32_t)value;

    /* Watch GPR22 bit0 (CM7_CPUWAIT) transitions */
    if (offset == IMX8MP_GPR22_OFF) {
        imx8mp_gpr_update_cm7_run(s, s->gpr[IMX8MP_GPR22_OFF >> 2]);
    }

}

static const MemoryRegionOps imx8mp_gpr_ops = {
    .read = imx8mp_gpr_read,
    .write = imx8mp_gpr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    .unaligned = false,
    },
};

static ResettablePhases imx8mp_gpr_parent_phases;

static void imx8mp_gpr_reset_enter(Object *obj, ResetType type)
{
    IMX8MPGPRState *s = IMX8MP_GPR(obj);

    if (imx8mp_gpr_parent_phases.enter) {
        imx8mp_gpr_parent_phases.enter(obj, type);
    }

    memset(s->gpr, 0, sizeof(s->gpr));

    s->gpr[IOMUXC_GPR_GPR0] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR1] = 0x00010000;
    s->gpr[IOMUXC_GPR_GPR2] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR3] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR4] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR5] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR6] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR7] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR8] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR9] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR10] = 0x00000008;
    s->gpr[IOMUXC_GPR_GPR11] = 0x00000200;
    s->gpr[IOMUXC_GPR_GPR12] = 0x00004000;
    s->gpr[IOMUXC_GPR_GPR13] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR14] = 0x03494000;
    s->gpr[IOMUXC_GPR_GPR15] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR16] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR17] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR18] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR19] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR20] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR21] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR22] = 0x00000001;
    s->gpr[IOMUXC_GPR_GPR23] = 0x00000000;
    s->gpr[IOMUXC_GPR_GPR24] = 0x00000000;
}

static void imx8mp_gpr_reset_hold(Object *obj, ResetType type)
{
    IMX8MPGPRState *s = IMX8MP_GPR(obj);

    if (imx8mp_gpr_parent_phases.hold) {
        imx8mp_gpr_parent_phases.hold(obj, type);
    }

    /* Drive cm7_run_irq output to match reset value of CPUWAIT */
    imx8mp_gpr_update_cm7_run(s, s->gpr[IMX8MP_GPR22_OFF >> 2]);
}

static const VMStateDescription imx8mp_gpr_vmstate = {
    .name = TYPE_IMX8MP_GPR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(gpr, IMX8MPGPRState, IOMUXC_GPR_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx8mp_gpr_init(Object *obj)
{
    IMX8MPGPRState *s = IMX8MP_GPR(obj);
    memory_region_init_io(&s->mmio, obj, &imx8mp_gpr_ops, s,
                          TYPE_IMX8MP_GPR, sizeof(s->gpr));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->cm7_run_irq);
}

static void imx8mp_gpr_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    resettable_class_set_parent_phases(rc,
                                       imx8mp_gpr_reset_enter,
                                       imx8mp_gpr_reset_hold,
                                       NULL,
                                       &imx8mp_gpr_parent_phases);

    dc->vmsd = &imx8mp_gpr_vmstate;
    dc->desc = "i.MX8MP IOMUXC GPR";
}

static const TypeInfo imx8mp_gpr_info[] = {
    {
        .name = TYPE_IMX8MP_GPR,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(IMX8MPGPRState),
        .instance_init = imx8mp_gpr_init,
        .class_init = imx8mp_gpr_class_init,
    }
};

DEFINE_TYPES(imx8mp_gpr_info);
