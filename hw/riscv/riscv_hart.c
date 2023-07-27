/*
 * QEMU RISCV Hart Array
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Holds the state of a homogeneous array of RISC-V harts
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv_hart.h"

static Property riscv_harts_props[] = {
    DEFINE_PROP_UINT32("num-harts", RISCVHartArrayState, num_harts, 1),
    DEFINE_PROP_UINT32("hartid-base", RISCVHartArrayState, hartid_base, 0),
    DEFINE_PROP_STRING("cpu-type", RISCVHartArrayState, cpu_type),
    DEFINE_PROP_UINT64("resetvec", RISCVHartArrayState, resetvec,
                       DEFAULT_RSTVEC),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_harts_cpu_reset(void *opaque)
{
    RISCVCPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}

static bool riscv_hart_realize(RISCVHartArrayState *s, int idx,
                               char *cpu_type, size_t size, Error **errp)
{
    RISCVCPU *hart = riscv_array_get_hart(s, idx);
    object_initialize_child_internal(OBJECT(s), "harts[*]",
                                    hart, size, cpu_type);
    qdev_prop_set_uint64(DEVICE(hart), "resetvec", s->resetvec);
    hart->env.mhartid = s->hartid_base + idx;
    qemu_register_reset(riscv_harts_cpu_reset, hart);
    return qdev_realize(DEVICE(hart), NULL, errp);
}

static void riscv_harts_realize(DeviceState *dev, Error **errp)
{
    RISCVHartArrayState *s = RISCV_HART_ARRAY(dev);
    size_t size = object_type_get_instance_size(s->cpu_type);
    int n;

    s->harts = g_new0(RISCVCPU *, s->num_harts);

    for (n = 0; n < s->num_harts; n++) {
        s->harts[n] = RISCV_CPU(object_new(s->cpu_type));
        if (!riscv_hart_realize(s, n, s->cpu_type, size, errp)) {
            return;
        }
    }
}

static void riscv_harts_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, riscv_harts_props);
    dc->realize = riscv_harts_realize;
}

static const TypeInfo riscv_harts_info = {
    .name          = TYPE_RISCV_HART_ARRAY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVHartArrayState),
    .class_init    = riscv_harts_class_init,
};

static void riscv_harts_register_types(void)
{
    type_register_static(&riscv_harts_info);
}

type_init(riscv_harts_register_types)
