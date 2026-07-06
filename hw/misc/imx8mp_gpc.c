/*
 * i.MX 8M Plus GPC(General Power Controller)
 *
 * Copyright (c) 2026, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "hw/core/resettable.h"
#include "hw/misc/imx8mp_gpc.h"
#include "migration/vmstate.h"

#define IMX8MP_GPC_ACK_BIT31 0x80000000u

#define IMX8MP_GPC_STAT_BUSY_BIT (1u << 5)

static uint64_t imx8mp_gpc_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX8MPGPCState *s = opaque;
    uint32_t idx = offset >> 2;

    if (size != 4 || (offset & 3) || offset >= IMX8MP_GPC_MMIO_SIZE) {
        return 0;
    }

    /*
     * CM7 firmware polls GPC+0xD8 and loops while bit5 is 1.
     * Emulate "ready" by forcing bit5 low on reads.
     */
    if (idx == IMX8MP_GPC_PU_PGC_SW_PUP_REQ) {
        return s->regs[idx] & ~IMX8MP_GPC_STAT_BUSY_BIT;
    }

    return s->regs[idx];
}

static void imx8mp_gpc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    IMX8MPGPCState *s = opaque;
    uint32_t idx = offset >> 2;

    if (size != 4 || (offset & 3) || offset >= IMX8MP_GPC_MMIO_SIZE) {
        return;
    }

    /*
     * CM7 firmware sets bit15 in GPC+0x190 and then polls until the value
     * becomes negative (bit31 set). Emulate the hardware ACK by forcing
     * bit31 high on writes to this register.
     */
    if (idx == IMX8MP_GPC_POLL_REG) {
        s->regs[idx] = ((uint32_t)value) | IMX8MP_GPC_ACK_BIT31;
    } else {
        s->regs[idx] = (uint32_t)value;
    }

}

static const MemoryRegionOps imx8mp_gpc_ops = {
    .read = imx8mp_gpc_read,
    .write = imx8mp_gpc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    .unaligned = false,
    },
};

static ResettablePhases imx8mp_gpc_parent_phases;

static void imx8mp_gpc_reset_enter(Object *obj, ResetType type)
{
    IMX8MPGPCState *s = IMX8MP_GPC(obj);

    if (imx8mp_gpc_parent_phases.enter) {
        imx8mp_gpc_parent_phases.enter(obj, type);
    }

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * Unblock CM7 firmware polling loop: return a negative value at 0x190.
     * Any value with bit31 set works.
     */
    s->regs[IMX8MP_GPC_POLL_REG] = IMX8MP_GPC_ACK_BIT31;

    /* Default "ready" for the earlier poll at 0xD8: ensure busy bit is clear */
    s->regs[IMX8MP_GPC_PU_PGC_SW_PUP_REQ] = 0x00000000;

}

static void imx8mp_gpc_reset_hold(Object *obj, ResetType type)
{
    if (imx8mp_gpc_parent_phases.hold) {
        imx8mp_gpc_parent_phases.hold(obj, type);
    }
}

static const VMStateDescription imx8mp_gpc_vmstate = {
    .name = TYPE_IMX8MP_GPC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMX8MPGPCState, IMX8MP_GPC_MMIO_SIZE / 4),
        VMSTATE_END_OF_LIST()
    },
};

static void imx8mp_gpc_init(Object *obj)
{
    IMX8MPGPCState *s = IMX8MP_GPC(obj);
    memory_region_init_io(&s->iomem, obj, &imx8mp_gpc_ops, s,
                          TYPE_IMX8MP_GPC, IMX8MP_GPC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void imx8mp_gpc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    resettable_class_set_parent_phases(rc,
                                       imx8mp_gpc_reset_enter,
                                       imx8mp_gpc_reset_hold,
                                       NULL,
                                       &imx8mp_gpc_parent_phases);

    dc->vmsd = &imx8mp_gpc_vmstate;
    dc->desc = "i.MX8MP GPC(General Power Controller)";
}

static const TypeInfo imx8mp_gpc_info[] = {
    {
    .name = TYPE_IMX8MP_GPC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX8MPGPCState),
    .instance_init = imx8mp_gpc_init,
    .class_init = imx8mp_gpc_class_init,
    }
};

DEFINE_TYPES(imx8mp_gpc_info);
