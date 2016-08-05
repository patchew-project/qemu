/*
 * QEMU PowerPC PowerNV CPU model
 *
 * Copyright (c) IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "target-ppc/cpu.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"

static void powernv_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    MachineState *machine = MACHINE(qdev_get_machine());
    sPowerNVMachineState *pnv = POWERNV_MACHINE(machine);

    cpu_reset(cs);

    env->spr[SPR_PIR] = ppc_get_vcpu_dt_id(cpu);
    env->spr[SPR_HIOR] = 0;
    env->gpr[3] = pnv->fdt_addr;
    env->nip = 0x10;
    env->msr |= MSR_HVB;
}

static void powernv_cpu_init(PowerPCCPU *cpu, Error **errp)
{
    CPUPPCState *env = &cpu->env;

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, PNV_TIMEBASE_FREQ);

    /* MSR[IP] doesn't exist nowadays */
    env->msr_mask &= ~(1 << 6);

    qemu_register_reset(powernv_cpu_reset, cpu);
    powernv_cpu_reset(cpu);
}

static void powernv_cpu_core_realize_child(Object *child, Error **errp)
{
    Error *local_err = NULL;
    CPUState *cs = CPU(child);
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    object_property_set_bool(child, true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    powernv_cpu_init(cpu, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void powernv_cpu_core_realize(DeviceState *dev, Error **errp)
{
    PowerNVCPUCore *pc = POWERNV_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    PowerNVCPUClass *pcc = POWERNV_CPU_GET_CLASS(OBJECT(dev));
    const char *typename = object_class_get_name(pcc->cpu_oc);
    size_t size = object_type_get_instance_size(typename);
    Error *local_err = NULL;
    void *obj;
    int i, j;


    pc->threads = g_malloc0(size * cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        char id[32];
        CPUState *cs;

        obj = pc->threads + i * size;

        object_initialize(obj, size, typename);
        cs = CPU(obj);
        cs->cpu_index = cc->core_id + i;
        snprintf(id, sizeof(id), "thread[%d]", i);
        object_property_add_child(OBJECT(pc), id, obj, &local_err);
        if (local_err) {
            goto err;
        }
        object_unref(obj);
    }

    for (j = 0; j < cc->nr_threads; j++) {
        obj = pc->threads + j * size;

        powernv_cpu_core_realize_child(obj, &local_err);
        if (local_err) {
            goto err;
        }
    }
    return;

err:
    while (--i >= 0) {
        obj = pc->threads + i * size;
        object_unparent(obj);
    }
    g_free(pc->threads);
    error_propagate(errp, local_err);
}

/*
 * Grow this list or merge with SPAPRCoreInfo which is very similar
 */
static const char *powernv_core_models[] = { "POWER8" };

static void powernv_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerNVCPUClass *pcc = POWERNV_CPU_CLASS(oc);

    dc->realize = powernv_cpu_core_realize;
    pcc->cpu_oc = cpu_class_by_name(TYPE_POWERPC_CPU, data);
}

static const TypeInfo powernv_cpu_core_info = {
    .name           = TYPE_POWERNV_CPU_CORE,
    .parent         = TYPE_CPU_CORE,
    .instance_size  = sizeof(PowerNVCPUCore),
    .class_size     = sizeof(PowerNVCPUClass),
    .abstract       = true,
};

static void powernv_cpu_core_register_types(void)
{
    int i ;

    type_register_static(&powernv_cpu_core_info);
    for (i = 0; i < ARRAY_SIZE(powernv_core_models); ++i) {
        TypeInfo ti = {
            .parent = TYPE_POWERNV_CPU_CORE,
            .instance_size = sizeof(PowerNVCPUCore),
            .class_init = powernv_cpu_core_class_init,
            .class_data = (void *) powernv_core_models[i],
        };
        ti.name = powernv_cpu_core_typename(powernv_core_models[i]);
        type_register(&ti);
        g_free((void *)ti.name);
    }
}

type_init(powernv_cpu_core_register_types)

char *powernv_cpu_core_typename(const char *model)
{
    return g_strdup_printf("%s-" TYPE_POWERNV_CPU_CORE, model);
}
