/*
 * QEMU model of Xilinx I/O Module PIT
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/register.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/timer/xlnx-pmu-iomod-pit.h"

#ifndef XLNX_ZYNQMP_IOMODULE_PIT_ERR_DEBUG
#define XLNX_ZYNQMP_IOMODULE_PIT_ERR_DEBUG 0
#endif

REG32(PIT_PRELOAD, 0x00)
REG32(PIT_COUNTER, 0x04)
REG32(PIT_CONTROL, 0x08)
    FIELD(PIT_CONTROL, PRELOAD, 1, 1)
    FIELD(PIT_CONTROL, EN, 0, 1)

static uint64_t xlnx_iomod_pit_ctr_pr(RegisterInfo *reg, uint64_t val)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(reg->opaque);
    uint32_t ret;

    if (s->ps_enable) {
        ret = s->ps_counter;
    } else {
        ret = ptimer_get_count(s->ptimer);
    }

    return ret;
}

static void xlnx_iomod_pit_control_pw(RegisterInfo *reg, uint64_t val)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(reg->opaque);

    ptimer_stop(s->ptimer);

    if (val & R_PIT_CONTROL_EN_MASK) {
        if (s->ps_enable) {
            /* pre-scalar mode do-Nothing here */
            s->ps_counter = s->regs[R_PIT_PRELOAD];
        } else {
            ptimer_set_limit(s->ptimer, s->regs[R_PIT_PRELOAD], 1);
            ptimer_run(s->ptimer, !(val & R_PIT_CONTROL_PRELOAD_MASK));

        }
    }
}

static const RegisterAccessInfo xlnx_iomod_pit_regs_info[] = {
    { .name = "PIT_PRELOAD",  .addr = A_PIT_PRELOAD,
        .ro = 0xffffffff,
    },{ .name = "PIT_COUNTER",  .addr = A_PIT_COUNTER,
        .ro = 0xffffffff,
        .post_read = xlnx_iomod_pit_ctr_pr,
    },{ .name = "PIT_CONTROL",  .addr = A_PIT_CONTROL,
        .rsvd = 0xfffffffc,
        .post_write = xlnx_iomod_pit_control_pw,
    }
};

static void xlnx_iomod_pit_timer_hit(void *opaque)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(opaque);

    qemu_irq_pulse(s->irq);

    /* hit_out to make another pit move it's counter in pre-scalar mode. */
    qemu_irq_pulse(s->hit_out);
}

static void xlnx_iomod_pit_ps_config(void *opaque, int n, int level)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(opaque);

    s->ps_enable = level;
}

static void xlnx_iomod_pit_ps_hit_in(void *opaque, int n, int level)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(opaque);

    if (!ARRAY_FIELD_EX32(s->regs, PIT_CONTROL, EN)) {
        /* PIT disabled */
        return;
    }

    /* Count only on positive edge */
    if (!s->ps_level && level) {
        if (s->ps_counter) {
            s->ps_counter--;
        }
        s->ps_level = level;
    } else {
        /* Not on positive edge */
        s->ps_level = level;
        return;
    }

    /* If timer expires, try to preload or stop */
    if (s->ps_counter == 0) {
        xlnx_iomod_pit_timer_hit(opaque);

        /* Check for pit preload/one-shot mode */
        if (ARRAY_FIELD_EX32(s->regs, PIT_CONTROL, PRELOAD)) {
            /* Preload Mode, Reload the ps_counter */
            s->ps_counter = s->regs[R_PIT_PRELOAD];
        } else {
            /* One-Shot mode, turn off the timer */
            s->regs[R_PIT_CONTROL] &= ~R_PIT_CONTROL_PRELOAD_MASK;
        }
    }
}

static void xlnx_iomod_pit_reset(DeviceState *dev)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    s->ps_level = false;
}

static const MemoryRegionOps xlnx_iomod_pit_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlnx_iomod_pit_realize(DeviceState *dev, Error **errp)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(dev);

    s->bh = qemu_bh_new(xlnx_iomod_pit_timer_hit, s);
    s->ptimer = ptimer_init(s->bh, PTIMER_POLICY_DEFAULT);
    ptimer_set_freq(s->ptimer, s->frequency);

    /* IRQ out to pulse when present timer expires/reloads */
    qdev_init_gpio_out_named(dev, &s->hit_out, "ps_hit_out", 1);

    /* IRQ in to enable pre-scalar mode. Routed from gpo1 */
    qdev_init_gpio_in_named(dev, xlnx_iomod_pit_ps_config, "ps_config", 1);

    /* hit_out of neighbouring PIT is received as hit_in */
    qdev_init_gpio_in_named(dev, xlnx_iomod_pit_ps_hit_in, "ps_hit_in", 1);
}

static void xlnx_iomod_pit_init(Object *obj)
{
    XlnxPMUPIT *s = XLNX_ZYNQMP_IOMODULE_PIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_ZYNQMP_IOMODULE_PIT,
                       XLNX_ZYNQMP_IOMODULE_PIT_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), xlnx_iomod_pit_regs_info,
                              ARRAY_SIZE(xlnx_iomod_pit_regs_info),
                              s->regs_info, s->regs,
                              &xlnx_iomod_pit_ops,
                              XLNX_ZYNQMP_IOMODULE_PIT_ERR_DEBUG,
                              XLNX_ZYNQMP_IOMODULE_PIT_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_xlnx_iomod_pit = {
    .name = TYPE_XLNX_ZYNQMP_IOMODULE_PIT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static Property xlnx_iomod_pit_properties[] = {
    DEFINE_PROP_UINT32("frequency", XlnxPMUPIT, frequency, 66000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void xlnx_iomod_pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xlnx_iomod_pit_reset;
    dc->realize = xlnx_iomod_pit_realize;
    dc->props = xlnx_iomod_pit_properties;
    dc->vmsd = &vmstate_xlnx_iomod_pit;
}

static const TypeInfo xlnx_iomod_pit_info = {
    .name          = TYPE_XLNX_ZYNQMP_IOMODULE_PIT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxPMUPIT),
    .class_init    = xlnx_iomod_pit_class_init,
    .instance_init = xlnx_iomod_pit_init,
};

static void xlnx_iomod_pit_register_types(void)
{
    type_register_static(&xlnx_iomod_pit_info);
}

type_init(xlnx_iomod_pit_register_types)
