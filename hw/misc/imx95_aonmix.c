/*
 * NXP i.MX 95 BLK_CTRL_S_AONMIX - minimal model for the M7 CPU-WAIT gate
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Covers BLK_CTRL_S_AONMIX at 0x444f0000. The block is mostly plain
 * configuration storage, modelled here as a RAM-backed register file (an
 * improvement over the previous logging stub, which read back as zero).
 *
 * The one load-bearing register is M7_CFG (offset 0x124). Its WAIT bit
 * (bit 4) is the Cortex-M7 hold/run gate the System Manager uses to manage
 * the M7's lifecycle: CPU_RunModeGet reads it (WAIT set => HOLD, clear =>
 * START) and CPU_WaitSet sets/clears it. At reset WAIT is set, so the SM
 * sees the M7 held and runs its full DEV_SM_CpuStart sequence - releasing
 * CPUWAIT (which we surface as the m7-run gpio) and, crucially, enabling
 * the CM7_SYSRESETREQ fault IRQ so the SM can later cold-reset the M7 LM.
 * A WAIT 1->0 transition releases the M7; 0->1 holds it.
 *
 * INITVTOR (0x108) - the M7 boot vector the SM programs - is stored but
 * unused: our M7 boots from its ITCM reset vector (init-svtor = 0).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_IMX95_AONMIX "imx95.aonmix"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95AonmixState, IMX95_AONMIX)

#define IMX95_AONMIX_REG_SIZE   0x10000
#define IMX95_AONMIX_NUM_WORDS  (IMX95_AONMIX_REG_SIZE / 4)

#define AONMIX_M7_CFG           0x124       /* M7 configure register */
#define AONMIX_M7_CFG_WAIT      0x10        /* bit 4: M7 CPU-WAIT (hold) */

struct IMX95AonmixState {
    SysBusDevice    parent_obj;
    MemoryRegion    iomem;
    uint32_t        regs[IMX95_AONMIX_NUM_WORDS];

    /*
     * M7 run gate, driven by M7_CFG.WAIT (level: 1 = released/run,
     * 0 = held). The machine wires this to a handler that resets+resumes
     * the M7 on release and halts it on hold.
     */
    qemu_irq        m7_run;
};

static uint64_t imx95_aonmix_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95AonmixState *s = opaque;

    trace_imx95_aonmix_read(offset);
    return s->regs[offset / 4];
}

static void imx95_aonmix_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    IMX95AonmixState *s = opaque;

    trace_imx95_aonmix_write(offset, value);

    /*
     * M7_CFG.WAIT toggling is the SM holding (set) or releasing (clear) the
     * M7. Surface the released state on the m7-run line so the machine
     * cycles the core. Only fires on an actual WAIT transition.
     */
    if (offset == AONMIX_M7_CFG) {
        uint32_t old = s->regs[offset / 4];
        uint32_t new = (uint32_t)value;

        s->regs[offset / 4] = new;
        if ((old ^ new) & AONMIX_M7_CFG_WAIT) {
            int run = (new & AONMIX_M7_CFG_WAIT) ? 0 : 1;
            trace_imx95_aonmix_m7_gate(run);
            qemu_set_irq(s->m7_run, run);
        }
        return;
    }

    s->regs[offset / 4] = value;
}

static const MemoryRegionOps imx95_aonmix_ops = {
    .read = imx95_aonmix_read,
    .write = imx95_aonmix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_aonmix_reset_hold(Object *obj, ResetType type)
{
    IMX95AonmixState *s = IMX95_AONMIX(obj);

    memset(s->regs, 0, sizeof(s->regs));
    /* M7 held at reset: the SM sees HOLD and runs its full CpuStart. */
    s->regs[AONMIX_M7_CFG / 4] = AONMIX_M7_CFG_WAIT;
}

static void imx95_aonmix_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95AonmixState *s = IMX95_AONMIX(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_aonmix_ops, s,
                          TYPE_IMX95_AONMIX, IMX95_AONMIX_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_out_named(DEVICE(obj), &s->m7_run, "m7-run", 1);
}

static const VMStateDescription vmstate_imx95_aonmix = {
    .name = TYPE_IMX95_AONMIX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMX95AonmixState, IMX95_AONMIX_NUM_WORDS),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_aonmix_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx95_aonmix;
    rc->phases.hold = imx95_aonmix_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 BLK_CTRL_S_AONMIX (M7 CPU-WAIT gate)";
}

static const TypeInfo imx95_aonmix_info = {
    .name           = TYPE_IMX95_AONMIX,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95AonmixState),
    .instance_init  = imx95_aonmix_init,
    .class_init     = imx95_aonmix_class_init,
};

static void imx95_aonmix_register_types(void)
{
    type_register_static(&imx95_aonmix_info);
}

type_init(imx95_aonmix_register_types)
