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
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/cpu/cpus.h"

void riscv_hart_array_realize(RISCVHartArrayState *state, Error **errp)
{
    /* disable the clustering */
    cpus_disable_clustering(CPUS(state));
    qdev_realize(DEVICE(state), NULL, errp);
}

static Property riscv_harts_props[] = {
    DEFINE_PROP_UINT32("hartid-base", RISCVHartArrayState, hartid_base, 0),
    DEFINE_PROP_UINT64("resetvec", RISCVHartArrayState, resetvec,
                       DEFAULT_RSTVEC),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_harts_configure_cpu(CpusState *base, CPUState *cpu,
                                      unsigned i)
{
    RISCVHartArrayState *s = RISCV_HART_ARRAY(base);
    DeviceState *cpudev = DEVICE(cpu);
    CPURISCVState *cpuenv = cpu->env_ptr;

    qdev_prop_set_uint64(cpudev, "resetvec", s->resetvec);
    cpuenv->mhartid = s->hartid_base + i;
}

static void riscv_harts_init(Object *obj)
{
    /* add a temporary property to keep num-harts */
    object_property_add_alias(obj, "num-harts", obj, "num-cpus");
}

static void riscv_harts_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CpusClass *cc = CPUS_CLASS(klass);

    device_class_set_props(dc, riscv_harts_props);

    cc->base_cpu_type = TYPE_RISCV_CPU;
    cc->configure_cpu = riscv_harts_configure_cpu;
}

static const TypeInfo riscv_harts_info = {
    .name          = TYPE_RISCV_HART_ARRAY,
    .parent        = TYPE_CPUS,
    .instance_size = sizeof(RISCVHartArrayState),
    .instance_init = riscv_harts_init,
    .class_init    = riscv_harts_class_init,
};

static void riscv_harts_register_types(void)
{
    type_register_static(&riscv_harts_info);
}

type_init(riscv_harts_register_types)
