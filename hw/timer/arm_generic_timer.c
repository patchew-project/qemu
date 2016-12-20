/*
 * QEMU model of the ARM Generic Timer
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Written by Alistair Francis <alistair.francis@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/timer/arm_generic_timer.h"
#include "qemu/timer.h"
#include "qemu/log.h"

#ifndef ARM_GEN_TIMER_ERR_DEBUG
#define ARM_GEN_TIMER_ERR_DEBUG 0
#endif

static void counter_control_postw(RegisterInfo *reg, uint64_t val64)
{
    ARMGenTimer *s = ARM_GEN_TIMER(reg->opaque);
    bool new_status = extract32(s->regs[R_CNTCR],
                                R_CNTCR_EN_SHIFT,
                                R_CNTCR_EN_LENGTH);
    uint64_t current_ticks;

    current_ticks = muldiv64(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL),
                             NANOSECONDS_PER_SECOND, 1000000);

    if ((s->enabled && !new_status) ||
        (!s->enabled && new_status)) {
        /* The timer is being disabled or enabled */
        s->tick_offset = current_ticks - s->tick_offset;
    }

    s->enabled = new_status;
}

static uint64_t counter_value_postr(RegisterInfo *reg)
{
    ARMGenTimer *s = ARM_GEN_TIMER(reg->opaque);
    uint64_t current_ticks, total_ticks;

    if (s->enabled) {
        current_ticks = muldiv64(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL),
                                 NANOSECONDS_PER_SECOND, 1000000);
        total_ticks = current_ticks - s->tick_offset;
    } else {
        /* Timer is disabled, return the time when it was disabled */
        total_ticks = s->tick_offset;
    }

    return total_ticks;
}

static uint64_t counter_low_value_postr(RegisterInfo *reg, uint64_t val64)
{
    return (uint32_t) counter_value_postr(reg);
}

static uint64_t counter_high_value_postr(RegisterInfo *reg, uint64_t val64)
{
    return (uint32_t) (counter_value_postr(reg) >> 32);
}

static RegisterAccessInfo arm_gen_timer_regs_info[] = {
    {   .name = "CNTCR",
        .addr = A_CNTCR,
        .rsvd = 0xfffffffc,
        .post_write = counter_control_postw,
    },{ .name = "CNTSR",
        .addr = A_CNTSR,
        .rsvd = 0xfffffffd, .ro = 0x2,
    },{ .name = "CNTCV_LOWER",
        .addr = A_CNTCV_LOWER,
        .post_read = counter_low_value_postr,
    },{ .name = "CNTCV_UPPER",
        .addr = A_CNTCV_UPPER,
        .post_read = counter_high_value_postr,
    },{ .name = "CNTFID0",
        .addr = A_CNTFID0,
    }
    /* We don't model CNTFIDn */
    /* We don't model the CounterID registers either */
};

static RegisterAccessInfo arm_gen_timer_read_regs_info[] = {
    {   .name = "CNTCV_READ_LOWER",
        .addr = A_CNTCV_READ_LOWER, .ro = 0xFFFF,
        .post_read = counter_low_value_postr,
    },{ .name = "CNTCV_READ_UPPER",
        .addr = A_CNTCV_READ_UPPER, .ro = 0xFFFF,
        .post_read = counter_high_value_postr,
    }
    /* We don't model the CounterID registers */
};

static void arm_gen_timer_reset(DeviceState *dev)
{
    ARMGenTimer *s = ARM_GEN_TIMER(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    s->tick_offset = 0;
    s->enabled = false;
}

static MemTxResult arm_gen_timer_read(void *opaque,  hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs)
{
    /* Reads are always supported, just blindly pass them through */
    *data = register_read_memory(opaque, addr, size);

    return MEMTX_OK;
}

static MemTxResult arm_gen_timer_write(void *opaque, hwaddr addr,
                                       uint64_t data, unsigned size,
                                       MemTxAttrs attrs)
{
    /* Block insecure writes */
    if (!attrs.secure) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Non secure writes to the system timestamp generator " \
                      "are invalid\n");
        return MEMTX_ERROR;
    }

    register_write_memory(opaque, addr, data, size);

    return MEMTX_OK;
}

static const MemoryRegionOps arm_gen_timer_ops = {
    .read_with_attrs = arm_gen_timer_read,
    .write_with_attrs = arm_gen_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_arm_gen_timer = {
    .name = TYPE_ARM_GEN_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ARMGenTimer, R_ARM_GEN_TIMER_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void arm_gen_timer_init(Object *obj)
{
    ARMGenTimer *s = ARM_GEN_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    /* Create the ControlBase memory region */
    memory_region_init_io(&s->iomem, obj, &arm_gen_timer_ops, s,
                          TYPE_ARM_GEN_TIMER, R_ARM_GEN_TIMER_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), arm_gen_timer_regs_info,
                              ARRAY_SIZE(arm_gen_timer_regs_info),
                              s->regs_info, s->regs,
                              &arm_gen_timer_ops,
                              ARM_GEN_TIMER_ERR_DEBUG,
                              R_ARM_GEN_TIMER_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                A_CNTCR,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Create the ReadBase memory region */
    memory_region_init_io(&s->iomem_read, obj, &arm_gen_timer_ops, s,
                          TYPE_ARM_GEN_TIMER "-read", R_ARM_GEN_TIMER_READ_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), arm_gen_timer_read_regs_info,
                              ARRAY_SIZE(arm_gen_timer_read_regs_info),
                              s->regs_read_info, s->regs_read,
                              &arm_gen_timer_ops,
                              ARM_GEN_TIMER_ERR_DEBUG,
                              R_ARM_GEN_TIMER_READ_MAX * 4);
    memory_region_add_subregion(&s->iomem_read,
                                R_CNTCV_READ_LOWER,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem_read);
}

static void arm_gen_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = arm_gen_timer_reset;
    dc->vmsd = &vmstate_arm_gen_timer;
}

static const TypeInfo arm_gen_timer_info = {
    .name          = TYPE_ARM_GEN_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMGenTimer),
    .class_init    = arm_gen_timer_class_init,
    .instance_init = arm_gen_timer_init,
};

static void arm_gen_timer_register_types(void)
{
    type_register_static(&arm_gen_timer_info);
}

type_init(arm_gen_timer_register_types)
