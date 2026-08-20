/*
 * NXP i.MX 95 SRC (System Reset Controller) mix-slice stub model
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Covers the SRC block at 0x44460000: SRC_GEN at offset 0, then the
 * per-power-domain "mix slice" register blocks (SRC_XSPR) starting at
 * offset 0x400, one every 0x400 (ANAMIX, AONMIX, ..., CCMSRCGPCMIX, ...).
 *
 * The System Manager powers a mix down by setting SLICE_SW_CTRL.PDN_SOFT
 * (bit 31, slice offset 0x20) and powers it up by clearing it, then polls
 * the read-only FUNC_STAT (slice offset 0xb4) until the power state
 * settles. QEMU has no power switches, so the transition is instantaneous:
 * FUNC_STAT is derived from the slice's SLICE_SW_CTRL.PDN_SOFT -
 *   powered up   -> RST_STAT released                  (0x00000004)
 *   powered down -> PSW off, ISO on, handshakes done    (0x00005511)
 * matching PWR_MIX_FUNC_STAT_PUP / _PDN in the SM's fsl_power.h. Everything
 * else is plain storage. Same status-mirrors-control idea as the GPC model.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_IMX95_SRC "imx95.src"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95SRCState, IMX95_SRC)

#define IMX95_SRC_REG_SIZE      0x10000
#define IMX95_SRC_NUM_WORDS     (IMX95_SRC_REG_SIZE / 4)

/* Mix-slice (SRC_XSPR) layout. SRC_GEN occupies the first slice-sized block. */
#define SRC_SLICE_STRIDE        0x400
#define SRC_SLICE_SW_CTRL       0x20    /* PDN_SOFT in bit 31 */
#define SRC_FUNC_STAT           0xb4    /* read-only power/reset status */
#define SRC_SLICE_SW_CTRL_PDN_SOFT  0x80000000u

/* FUNC_STAT values for the fully-up / fully-down states (SM fsl_power.h). */
#define SRC_FUNC_STAT_PUP       0x00000004u
#define SRC_FUNC_STAT_PDN       0x00005511u

/*
 * SRC_GEN.SCR - the boot-reset-release latch register at offset 0x10
 * within SRC_GEN (which sits at the SRC block base). Bit 12 is the
 * M7MIX release; per the i.MX 95 reference manual it is sticky:
 * "M7MIX will be held under reset until boot core writes this bit to 1.
 * Once this bit is set to 1, it will be locked." The SM's
 * DEV_SM_CpuStart(M7) path writes this bit during the LMM_Boot phase;
 * we use the 0->1 transition as a rising edge on the m7mix_release
 * gpio-out so the machine wrapper can release the M7 CPU from
 * start-powered-off (silicon-faithful M7 release).
 */
#define SRC_GEN_SCR_OFFSET                      0x10u
#define SRC_GEN_SCR_BOOT_RESET_RELEASE_M7MIX    (1u << 12)

/*
 * Per-slice reset line. SLICE_SW_CTRL.RST_RSTR_0 (bit 20) asserts the
 * slice's core reset; the read-only RSTR_STAT.RSTR_0_RST_STAT (bit 0, slice
 * offset 0xb8) reports it back. The SM's SRC_MixSetResetLine writes RST_RSTR_0
 * and SRC_MixGetResetLine polls RSTR_STAT until it matches, so - like
 * FUNC_STAT mirroring PDN_SOFT - we derive RSTR_STAT from RST_RSTR_0 so the
 * SM's assert/deassert wait loops converge.
 */
#define SRC_RSTR_STAT                           0xb8
/*
 * SLICE_SW_CTRL.RST_RSTR[3:0] live in bits [23:20]; RSTR_STAT.RST_STAT[3:0] in
 * bits [3:0]. The reset lines (M7MIX + sub-resets) each use one of these.
 */
#define SRC_SLICE_SW_CTRL_RST_RSTR_SHIFT        20

struct IMX95SRCState {
    SysBusDevice    parent_obj;
    MemoryRegion    iomem;
    uint32_t        regs[IMX95_SRC_NUM_WORDS];

    /*
     * Rising edge when SRC_GEN.SCR.BOOT_RESET_RELEASE_M7MIX goes 0 -> 1.
     * Wired by the machine to a handler that releases the M7 CPU,
     * complementing the existing reset-time release path (which
     * releases M7 when -device loader has staged firmware into ITCM,
     * used by tests that boot the M7 standalone without the SM).
     */
    qemu_irq        m7mix_release;
};

static uint64_t imx95_src_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95SRCState *s = opaque;

    /* FUNC_STAT in a mix slice mirrors that slice's SLICE_SW_CTRL.PDN_SOFT. */
    if (offset >= SRC_SLICE_STRIDE &&
        (offset & (SRC_SLICE_STRIDE - 1)) == SRC_FUNC_STAT) {
        hwaddr slice = offset & ~(hwaddr)(SRC_SLICE_STRIDE - 1);
        uint32_t ctrl = s->regs[(slice + SRC_SLICE_SW_CTRL) / 4];

        return (ctrl & SRC_SLICE_SW_CTRL_PDN_SOFT) ? SRC_FUNC_STAT_PDN
                                                   : SRC_FUNC_STAT_PUP;
    }

    /*
     * RSTR_STAT mirrors that slice's SLICE_SW_CTRL reset lines: the SM
     * asserts one of RST_RSTR[3:0] (SLICE_SW_CTRL bits [23:20]) and polls the
     * matching RST_STAT[3:0] (RSTR_STAT bits [3:0]) until it tracks. The M7 LM
     * reset walks several reset lines (M7MIX + associated sub-resets), each on
     * a different RST_RSTR bit, so mirror all four.
     */
    if (offset >= SRC_SLICE_STRIDE &&
        (offset & (SRC_SLICE_STRIDE - 1)) == SRC_RSTR_STAT) {
        hwaddr slice = offset & ~(hwaddr)(SRC_SLICE_STRIDE - 1);
        uint32_t ctrl = s->regs[(slice + SRC_SLICE_SW_CTRL) / 4];

        return (ctrl >> SRC_SLICE_SW_CTRL_RST_RSTR_SHIFT) & 0xf;
    }
    return s->regs[offset / 4];
}

static void imx95_src_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    IMX95SRCState *s = opaque;

    /*
     * SRC_GEN.SCR: bit 12 (M7MIX release) is sticky/locked once set per
     * the RM. Track the 0->1 transition and pulse the m7mix_release out
     * so the machine releases the M7 CPU.
     */
    if (offset == SRC_GEN_SCR_OFFSET) {
        uint32_t old = s->regs[offset / 4];
        uint32_t new = (uint32_t)value;

        /* Lock any bit that was already 1: hardware-sticky behaviour. */
        new |= old & SRC_GEN_SCR_BOOT_RESET_RELEASE_M7MIX;
        s->regs[offset / 4] = new;

        if (!(old & SRC_GEN_SCR_BOOT_RESET_RELEASE_M7MIX) &&
             (new & SRC_GEN_SCR_BOOT_RESET_RELEASE_M7MIX)) {
            trace_imx95_src_m7mix_release();
            qemu_irq_raise(s->m7mix_release);
        }
        return;
    }

    if (offset >= SRC_SLICE_STRIDE &&
        (offset & (SRC_SLICE_STRIDE - 1)) == SRC_SLICE_SW_CTRL) {
        trace_imx95_src_slice_ctrl(offset, (uint32_t)value);
    }

    s->regs[offset / 4] = value;
}

static const MemoryRegionOps imx95_src_ops = {
    .read = imx95_src_read,
    .write = imx95_src_write,
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

static void imx95_src_reset_hold(Object *obj, ResetType type)
{
    IMX95SRCState *s = IMX95_SRC(obj);

    memset(s->regs, 0, sizeof(s->regs));
}

static void imx95_src_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95SRCState *s = IMX95_SRC(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_src_ops, s,
                          TYPE_IMX95_SRC, IMX95_SRC_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /*
     * Named gpio-out for SRC_GEN.SCR.M7MIX rising edge. Connected by
     * the machine wrapper to a handler that releases the M7 CPU.
     */
    qdev_init_gpio_out_named(DEVICE(obj), &s->m7mix_release,
                             "m7mix-release", 1);
}

static const VMStateDescription vmstate_imx95_src = {
    .name = TYPE_IMX95_SRC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMX95SRCState, IMX95_SRC_NUM_WORDS),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_src_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx95_src;
    rc->phases.hold = imx95_src_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 SRC (stub)";
}

static const TypeInfo imx95_src_info = {
    .name           = TYPE_IMX95_SRC,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95SRCState),
    .instance_init  = imx95_src_init,
    .class_init     = imx95_src_class_init,
};

static void imx95_src_register_types(void)
{
    type_register_static(&imx95_src_info);
}

type_init(imx95_src_register_types)
