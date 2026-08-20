/*
 * NXP i.MX 95 DPU (Display Processing Unit) command-sequencer status stub
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The DPU is not modelled: this base machine is headless, and real display
 * scanout is a follow-on series. With the real System Manager powering the
 * display mix, however, Linux's dpu95 driver stops deferring and probes - and
 * its blit-engine bring-up busy-waits with NO
 * timeout on the command sequencer status register:
 *   dpu95_cs_wait_idle()       spins until CMDSEQ_STATUS.IDLE      is set
 *   dpu95_cs_wait_fifo_space() spins until CMDSEQ_STATUS.FIFOSPACE >= 192
 * A plain zero-returning stub never satisfies either, so the probe kthread
 * hangs forever, which in turn wedges wait_for_device_probe() in
 * kernel_init() and userspace never starts.
 *
 * This stub returns CMDSEQ_STATUS with IDLE set and a full FIFOSPACE so both
 * polls pass instantly; every other register reads back zero and writes are
 * dropped. That is enough to let the probe complete (the actual command
 * submission / fence path runs only at runtime, which we never reach). A
 * follow-on series that models the DPU for real (adding display scanout and
 * Wayland output) removes this stub.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "trace.h"

#define TYPE_IMX95_DPU "imx95.dpu"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95DPUState, IMX95_DPU)

#define IMX95_DPU_REG_SIZE 0x400000

struct IMX95DPUState {
    SysBusDevice    parent_obj;
    MemoryRegion    iomem;
};

/* Command-sequencer status, from the dpu95 blit register map. */
#define DPU_CMDSEQ_STATUS           0x1019c
#define DPU_CMDSEQ_STATUS_IDLE      0x40000000u
#define DPU_CMDSEQ_STATUS_FIFOSPACE 0x0001ffffu

static uint64_t imx95_dpu_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset == DPU_CMDSEQ_STATUS) {
        /*
         * Report the sequencer idle with a full FIFO. This is deliberately
         * NOT the reset value (0x41000080: IDLE set but FIFOSPACE = 128, below
         * the driver's threshold of 192), because the dpu95 probe polls
         * FIFOSPACE before the enable handshake that would raise it on real
         * silicon. Returning a constant full FIFO is sound only because the
         * model is probe-time-only: no actual command sequence is ever
         * submitted. If real DPU command submission is ever modelled,
         * FIFOSPACE must track the command FIFO occupancy instead.
         */
        uint32_t status = DPU_CMDSEQ_STATUS_IDLE | DPU_CMDSEQ_STATUS_FIFOSPACE;
        trace_imx95_dpu_cmdseq_status(status);
        return status;
    }
    return 0;
}

static void imx95_dpu_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
}

static const MemoryRegionOps imx95_dpu_ops = {
    .read = imx95_dpu_read,
    .write = imx95_dpu_write,
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

static void imx95_dpu_init(Object *obj)
{
    IMX95DPUState *s = IMX95_DPU(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_dpu_ops, s,
                          TYPE_IMX95_DPU, IMX95_DPU_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void imx95_dpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->desc = "NXP i.MX 95 DPU (status stub)";
}

static const TypeInfo imx95_dpu_info = {
    .name           = TYPE_IMX95_DPU,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95DPUState),
    .instance_init  = imx95_dpu_init,
    .class_init     = imx95_dpu_class_init,
};

static void imx95_dpu_register_types(void)
{
    type_register_static(&imx95_dpu_info);
}

type_init(imx95_dpu_register_types)
