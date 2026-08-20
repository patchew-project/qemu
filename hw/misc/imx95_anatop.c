/*
 * NXP i.MX 95 ANATOP / PLL stub model
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Covers the ANATOP block at 0x44480000. Its PLL instances live at
 * 0x44481000..0x444819ff, one every 0x100 (SYS_PLL1, AUDIO_PLL1/2,
 * VIDEO_PLL1, ARM_PLL, DRAM_PLL, HSIO_PLL, LDB_PLL). Each PLL register
 * uses the i.MX RW/SET/CLR/TOG quad layout (a 16-byte group: write +0 =
 * assign, +4 = set bits, +8 = clear bits, +C = toggle; reads return the
 * accumulated value), and each PLL has two read-only status words:
 * PLL_STATUS (+0xf0, bit0 = PLL_LOCK) and DFS_STATUS (+0xf4, bits[3:0] =
 * per-DFS clock-ok).
 *
 * The System Manager's DVFS path (DEV_SM_PerfA55FreqUpdate via
 * FRACTPLL_UpdateRate / FRACTPLL_UpdateDfsRate) powers a PLL up by setting
 * CTRL.POWERUP and then polls PLL_STATUS.PLL_LOCK; it enables a DFS and
 * polls DFS_STATUS.DFS_OK (an unbounded wait). QEMU has no analog PLLs, so
 * the transition is instantaneous: PLL_LOCK mirrors CTRL.POWERUP and a
 * DFS's DFS_OK bit mirrors its DFS_CTRL.ENABLE. Everything else is plain
 * storage. Same status-mirrors-control idea as the GPC/SRC models.
 *
 * Non-PLL parts of ANATOP (e.g. TMPSNS at +0x2000) are plain RAM here -
 * the quad/status behaviour is confined to the PLL window.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_IMX95_ANATOP "imx95.anatop"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95AnatopState, IMX95_ANATOP)

#define IMX95_ANATOP_REG_SIZE   0x10000
#define IMX95_ANATOP_NUM_WORDS  (IMX95_ANATOP_REG_SIZE / 4)

/* PLL window within ANATOP: [0x1000, 0x1a00), one PLL every 0x100. */
#define ANATOP_PLL_START        0x1000
#define ANATOP_PLL_END          0x1a00
#define PLL_BLOCK_SIZE          0x100

/* Per-PLL register offsets. */
#define PLL_CTRL_OFF            0x00    /* RW/SET/CLR/TOG, POWERUP in bit 0 */
#define PLL_DFS0_OFF            0x70    /* DFS[0] DFS_CTRL; step 0x20 */
#define PLL_DFS_STEP            0x20
#define PLL_NUM_DFS             4
#define PLL_STATUS_OFF          0xf0    /* RO: PLL_LOCK in bit 0 */
#define PLL_DFS_STATUS_OFF      0xf4    /* RO: DFS_OK in bits [3:0] */

#define PLL_CTRL_POWERUP        0x00000001u
#define PLL_STATUS_LOCK         0x00000001u
#define PLL_DFS_CTRL_ENABLE     0x80000000u

struct IMX95AnatopState {
    SysBusDevice    parent_obj;
    MemoryRegion    iomem;
    uint32_t        regs[IMX95_ANATOP_NUM_WORDS];
};

static bool anatop_in_pll_window(hwaddr offset)
{
    return offset >= ANATOP_PLL_START && offset < ANATOP_PLL_END;
}

static uint64_t imx95_anatop_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95AnatopState *s = opaque;

    trace_imx95_anatop_read(offset);

    if (anatop_in_pll_window(offset)) {
        hwaddr blk = offset & ~(hwaddr)(PLL_BLOCK_SIZE - 1);
        uint32_t reg = offset & (PLL_BLOCK_SIZE - 1);

        if (reg == PLL_STATUS_OFF) {
            /* PLL_LOCK mirrors CTRL.POWERUP (lock is instantaneous here). */
            uint32_t ctrl = s->regs[(blk + PLL_CTRL_OFF) / 4];
            return (ctrl & PLL_CTRL_POWERUP) ? PLL_STATUS_LOCK : 0;
        }
        if (reg == PLL_DFS_STATUS_OFF) {
            /* Each DFS_OK bit mirrors that DFS's DFS_CTRL.ENABLE. */
            uint32_t ok = 0;
            for (unsigned n = 0; n < PLL_NUM_DFS; n++) {
                hwaddr dfs = blk + PLL_DFS0_OFF + n * PLL_DFS_STEP;
                if (s->regs[dfs / 4] & PLL_DFS_CTRL_ENABLE) {
                    ok |= (1u << n);
                }
            }
            return ok;
        }
    }
    return s->regs[offset / 4];
}

static void imx95_anatop_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    IMX95AnatopState *s = opaque;

    trace_imx95_anatop_write(offset, value);

    if (anatop_in_pll_window(offset)) {
        uint32_t reg = offset & (PLL_BLOCK_SIZE - 1);

        /* PLL_STATUS / DFS_STATUS are read-only. */
        if (reg == PLL_STATUS_OFF || reg == PLL_DFS_STATUS_OFF) {
            return;
        }

        /* RW/SET/CLR/TOG aliases of one logical register (16-byte group). */
        hwaddr base = offset & ~(hwaddr)0xc;
        switch (offset & 0xc) {
        case 0x0:
            s->regs[offset / 4] = value;
            break;
        case 0x4:
            s->regs[base / 4] |= value;
            break;
        case 0x8:
            s->regs[base / 4] &= ~(uint32_t)value;
            break;
        case 0xc:
            s->regs[base / 4] ^= value;
            break;
        }
        return;
    }
    s->regs[offset / 4] = value;
}

static const MemoryRegionOps imx95_anatop_ops = {
    .read = imx95_anatop_read,
    .write = imx95_anatop_write,
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

static void imx95_anatop_reset_hold(Object *obj, ResetType type)
{
    IMX95AnatopState *s = IMX95_ANATOP(obj);

    memset(s->regs, 0, sizeof(s->regs));
}

static void imx95_anatop_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95AnatopState *s = IMX95_ANATOP(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_anatop_ops, s,
                          TYPE_IMX95_ANATOP, IMX95_ANATOP_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_imx95_anatop = {
    .name = TYPE_IMX95_ANATOP,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMX95AnatopState, IMX95_ANATOP_NUM_WORDS),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_anatop_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx95_anatop;
    rc->phases.hold = imx95_anatop_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 ANATOP/PLL (stub)";
}

static const TypeInfo imx95_anatop_info = {
    .name           = TYPE_IMX95_ANATOP,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95AnatopState),
    .instance_init  = imx95_anatop_init,
    .class_init     = imx95_anatop_class_init,
};

static void imx95_anatop_register_types(void)
{
    type_register_static(&imx95_anatop_info);
}

type_init(imx95_anatop_register_types)
