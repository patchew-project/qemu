/*
 * QEMU PowerPC PowerNV CPU Core model
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/log.h"
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
    PnvMachineState *pnv = POWERNV_MACHINE(machine);

    cpu_reset(cs);

    env->spr[SPR_PIR] = cs->cpu_index;
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

static void pnv_core_realize_child(Object *child, Error **errp)
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

static void pnv_core_realize(DeviceState *dev, Error **errp)
{
    PnvCore *pc = PNV_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    PnvCoreClass *pcc = PNV_CORE_GET_CLASS(OBJECT(dev));
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

        pnv_core_realize_child(obj, &local_err);
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
static const char *pnv_core_models[] = { "POWER8E", "POWER8", "POWER8NVL" };

static void pnv_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PnvCoreClass *pcc = PNV_CORE_CLASS(oc);

    dc->realize = pnv_core_realize;
    pcc->cpu_oc = cpu_class_by_name(TYPE_POWERPC_CPU, data);
}

static const TypeInfo pnv_core_info = {
    .name           = TYPE_PNV_CORE,
    .parent         = TYPE_CPU_CORE,
    .instance_size  = sizeof(PnvCore),
    .class_size     = sizeof(PnvCoreClass),
    .abstract       = true,
};


#define DTS_RESULT0     0x50000
#define DTS_RESULT1     0x50001

static bool pnv_core_xscom_read(XScomDevice *dev, uint32_t range,
                               uint32_t offset, uint64_t *out_val)
{
    switch (offset) {
    case DTS_RESULT0:
        *out_val = 0x26f024f023f0000ull;
        break;
    case DTS_RESULT1:
        *out_val = 0x24f000000000000ull;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Warning: reading reg=0x%08x", offset);
    }

   return true;
}

static bool pnv_core_xscom_write(XScomDevice *dev, uint32_t range,
                                uint32_t offset, uint64_t val)
{
    qemu_log_mask(LOG_GUEST_ERROR, "Warning: writing to reg=0x%08x", offset);
    return true;
}

#define EX_XSCOM_BASE 0x10000000
#define EX_XSCOM_SIZE 0x100000

static void pnv_core_xscom_realize(DeviceState *dev, Error **errp)
{
    XScomDevice *xd = XSCOM_DEVICE(dev);
    PnvCoreXScom *pnv_xd = PNV_CORE_XSCOM(dev);

    xd->ranges[0].addr = EX_XSCOM_BASE | P8_PIR2COREID(pnv_xd->core_pir) << 24;
    xd->ranges[0].size = EX_XSCOM_SIZE;
}

static Property pnv_core_xscom_properties[] = {
        DEFINE_PROP_UINT32("core-pir", PnvCoreXScom, core_pir, 0),
        DEFINE_PROP_END_OF_LIST(),
};

static void pnv_core_xscom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XScomDeviceClass *xdc = XSCOM_DEVICE_CLASS(klass);

    xdc->read = pnv_core_xscom_read;
    xdc->write = pnv_core_xscom_write;

    dc->realize = pnv_core_xscom_realize;
    dc->props = pnv_core_xscom_properties;
}

static const TypeInfo pnv_core_xscom_type_info = {
    .name          = TYPE_PNV_CORE_XSCOM,
    .parent        = TYPE_XSCOM_DEVICE,
    .instance_size = sizeof(PnvCoreXScom),
    .class_init    = pnv_core_xscom_class_init,
};

static void pnv_core_register_types(void)
{
    int i ;

    type_register_static(&pnv_core_xscom_type_info);
    type_register_static(&pnv_core_info);
    for (i = 0; i < ARRAY_SIZE(pnv_core_models); ++i) {
        TypeInfo ti = {
            .parent = TYPE_PNV_CORE,
            .instance_size = sizeof(PnvCore),
            .class_init = pnv_core_class_init,
            .class_data = (void *) pnv_core_models[i],
        };
        ti.name = pnv_core_typename(pnv_core_models[i]);
        type_register(&ti);
        g_free((void *)ti.name);
    }
}

type_init(pnv_core_register_types)

char *pnv_core_typename(const char *model)
{
    return g_strdup_printf(TYPE_PNV_CORE "-%s", model);
}
