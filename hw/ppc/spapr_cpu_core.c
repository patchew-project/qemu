/*
 * sPAPR CPU core device, acts as container of CPU thread devices.
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "hw/cpu/core.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/spapr.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "target/ppc/kvm_ppc.h"
#include "hw/ppc/ppc.h"
#include "target/ppc/mmu-hash64.h"
#include "sysemu/numa.h"
#include "sysemu/hw_accel.h"
#include "qemu/error-report.h"

static void spapr_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_reset(cs);

    /* All CPUs start halted.  CPU0 is unhalted from the machine level
     * reset code and the rest are explicitly started up by the guest
     * using an RTAS call */
    cs->halted = 1;

    env->spr[SPR_HIOR] = 0;
}

static void spapr_cpu_destroy(PowerPCCPU *cpu)
{
    qemu_unregister_reset(spapr_cpu_reset, cpu);
}

static void spapr_cpu_init(sPAPRMachineState *spapr, PowerPCCPU *cpu,
                           Error **errp)
{
    CPUPPCState *env = &cpu->env;

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, SPAPR_TIMEBASE_FREQ);

    /* Enable PAPR mode in TCG or KVM */
    cpu_ppc_set_papr(cpu, PPC_VIRTUAL_HYPERVISOR(spapr));

    qemu_register_reset(spapr_cpu_reset, cpu);
    spapr_cpu_reset(cpu);
}

/*
 * Return the sPAPR CPU core type for @model which essentially is the CPU
 * model specified with -cpu cmdline option.
 */
char *spapr_get_cpu_core_type(const char *model)
{
    char *core_type;
    gchar **model_pieces = g_strsplit(model, ",", 2);
    gchar *cpu_model = g_ascii_strdown(model_pieces[0], -1);
    g_strfreev(model_pieces);

    core_type = g_strdup_printf("%s-" TYPE_SPAPR_CPU_CORE, cpu_model);

    /* Check whether it exists or whether we have to look up an alias name */
    if (!object_class_by_name(core_type)) {
        const char *realmodel;

        g_free(core_type);
        core_type = NULL;
        realmodel = ppc_cpu_lookup_alias(cpu_model);
        if (realmodel) {
            core_type = spapr_get_cpu_core_type(realmodel);
        }
    }
    g_free(cpu_model);

    return core_type;
}

static void spapr_cpu_core_unrealizefn(DeviceState *dev, Error **errp)
{
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_GET_CLASS(OBJECT(dev));
    const char *typename = object_class_get_name(scc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    CPUCore *cc = CPU_CORE(dev);
    int i;

    for (i = 0; i < cc->nr_threads; i++) {
        void *obj = sc->threads + i * size;
        DeviceState *dev = DEVICE(obj);
        CPUState *cs = CPU(dev);
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        spapr_cpu_destroy(cpu);
        object_unparent(cpu->intc);
        cpu_remove_sync(cs);
        object_unparent(obj);
    }
    g_free(sc->threads);
}

static void spapr_cpu_core_realize_child(Object *child,
                                         sPAPRMachineState *spapr, Error **errp)
{
    Error *local_err = NULL;
    CPUState *cs = CPU(child);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    Object *obj;

    object_property_set_bool(child, true, "realized", &local_err);
    if (local_err) {
        goto error;
    }

    spapr_cpu_init(spapr, cpu, &local_err);
    if (local_err) {
        goto error;
    }

    obj = object_new(spapr->icp_type);
    object_property_add_child(child, "icp", obj, &error_abort);
    object_unref(obj);
    object_property_add_const_link(obj, ICP_PROP_XICS, OBJECT(spapr),
                                   &error_abort);
    object_property_add_const_link(obj, ICP_PROP_CPU, child, &error_abort);
    object_property_set_bool(obj, true, "realized", &local_err);
    if (local_err) {
        goto free_icp;
    }

    return;

free_icp:
    object_unparent(obj);
error:
    error_propagate(errp, local_err);
}

static void spapr_cpu_core_realize(DeviceState *dev, Error **errp)
{
    sPAPRMachineState *spapr;
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_GET_CLASS(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    const char *typename = object_class_get_name(scc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    Error *local_err = NULL;
    void *obj;
    int i, j;

    spapr = (sPAPRMachineState *) qdev_get_machine();
    if (!object_dynamic_cast((Object *) spapr, TYPE_SPAPR_MACHINE)) {
        error_setg(errp, "spapr-cpu-core needs a pseries machine");
        return;
    }

    sc->threads = g_malloc0(size * cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        char id[32];
        CPUState *cs;
        PowerPCCPU *cpu;

        obj = sc->threads + i * size;

        object_initialize(obj, size, typename);
        cs = CPU(obj);
        cpu = POWERPC_CPU(cs);
        cs->cpu_index = cc->core_id + i;
        cpu->vcpu_id = (cc->core_id * spapr->vsmt / smp_threads) + i;
        if (kvm_enabled() && !kvm_vcpu_id_is_valid(cpu->vcpu_id)) {
            error_setg(&local_err, "Can't create CPU with id %d in KVM",
                       cpu->vcpu_id);
            error_append_hint(&local_err, "Adjust the number of cpus to %d "
                              "or try to raise the number of threads per core\n",
                              cpu->vcpu_id * smp_threads / spapr->vsmt);
            goto err;
        }


        /* Set NUMA node for the threads belonged to core  */
        cpu->node_id = sc->node_id;

        snprintf(id, sizeof(id), "thread[%d]", i);
        object_property_add_child(OBJECT(sc), id, obj, &local_err);
        if (local_err) {
            goto err;
        }
        object_unref(obj);
    }

    for (j = 0; j < cc->nr_threads; j++) {
        obj = sc->threads + j * size;

        spapr_cpu_core_realize_child(obj, spapr, &local_err);
        if (local_err) {
            goto err;
        }
    }
    return;

err:
    while (--i >= 0) {
        obj = sc->threads + i * size;
        object_unparent(obj);
    }
    g_free(sc->threads);
    error_propagate(errp, local_err);
}

static Property spapr_cpu_core_properties[] = {
    DEFINE_PROP_INT32("node-id", sPAPRCPUCore, node_id, CPU_UNSET_NUMA_NODE_ID),
    DEFINE_PROP_END_OF_LIST()
};

void spapr_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_CLASS(oc);

    dc->realize = spapr_cpu_core_realize;
    dc->unrealize = spapr_cpu_core_unrealizefn;
    dc->props = spapr_cpu_core_properties;
    scc->cpu_class = cpu_class_by_name(TYPE_POWERPC_CPU, data);
    g_assert(scc->cpu_class);
}

#define DEFINE_SPAPR_CPU_CORE_TYPE(cpu_model) \
    {                                                   \
        .parent = TYPE_SPAPR_CPU_CORE,                  \
        .class_data = (void *) cpu_model,               \
        .class_init = spapr_cpu_core_class_init,        \
        .name = SPAPR_CPU_CORE_TYPE_NAME(cpu_model),    \
    }

static const TypeInfo spapr_cpu_core_type_infos[] = {
    {
        .name = TYPE_SPAPR_CPU_CORE,
        .parent = TYPE_CPU_CORE,
        .abstract = true,
        .instance_size = sizeof(sPAPRCPUCore),
        .class_size = sizeof(sPAPRCPUCoreClass),
    },
    DEFINE_SPAPR_CPU_CORE_TYPE("970_v2.2"),
    DEFINE_SPAPR_CPU_CORE_TYPE("970mp_v1.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("970mp_v1.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power5+_v2.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power7_v2.3"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power7+_v2.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power8_v2.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power8e_v2.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power8nvl_v1.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power9_v1.0"),
};

DEFINE_TYPES(spapr_cpu_core_type_infos)
