/*
 * QEMU PowerPC e500v2 ePAPR spinning code
 *
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Alexander Graf, <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * This code is not really a device, but models an interface that usually
 * firmware takes care of. It's used when QEMU plays the role of firmware.
 *
 * Specification:
 *
 * https://www.power.org/resources/downloads/Power_ePAPR_APPROVED_v1.1.pdf
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/hw_accel.h"
#include "e500.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"

#define MAX_CPUS 32

typedef struct spin_info {
    uint64_t addr;
    uint64_t r3;
    uint32_t resv;
    uint32_t pir;
    uint64_t reserved;
} QEMU_PACKED SpinInfo;

/*
 * QEMU interface:
 *  + QOM array property "cpus-qom-path": QOM canonical path of each CPU.
 */
#define TYPE_E500_SPIN "e500-spin"
OBJECT_DECLARE_SIMPLE_TYPE(SpinState, E500_SPIN)

struct SpinState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SpinInfo spin[MAX_CPUS];
    uint32_t cpu_count;
    char **cpu_canonical_path;
    CPUState **cpu;
};

static void spin_reset(DeviceState *dev)
{
    SpinState *s = E500_SPIN(dev);
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        SpinInfo *info = &s->spin[i];

        stl_p(&info->pir, i);
        stq_p(&info->r3, i);
        stq_p(&info->addr, 1);
    }
}

static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa,
                                     hwaddr len)
{
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 1);
    hwaddr size;

    size = (booke206_page_size_to_tlb(len) << MAS1_TSIZE_SHIFT);
    tlb->mas1 = MAS1_VALID | size;
    tlb->mas2 = (va & TARGET_PAGE_MASK) | MAS2_M;
    tlb->mas7_3 = pa & TARGET_PAGE_MASK;
    tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;
#ifdef CONFIG_KVM
    env->tlb_dirty = true;
#endif
}

static void spin_kick(CPUState *cs, run_on_cpu_data data)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    SpinInfo *curspin = data.host_ptr;
    hwaddr map_size = 64 * MiB;
    hwaddr map_start;

    cpu_synchronize_state(cs);
    stl_p(&curspin->pir, env->spr[SPR_BOOKE_PIR]);
    env->nip = ldq_p(&curspin->addr) & (map_size - 1);
    env->gpr[3] = ldq_p(&curspin->r3);
    env->gpr[4] = 0;
    env->gpr[5] = 0;
    env->gpr[6] = 0;
    env->gpr[7] = map_size;
    env->gpr[8] = 0;
    env->gpr[9] = 0;

    map_start = ldq_p(&curspin->addr) & ~(map_size - 1);
    mmubooke_create_initial_mapping(env, 0, map_start, map_size);

    cs->halted = 0;
    cs->exception_index = -1;
    cs->stopped = false;
    qemu_cpu_kick(cs);
}

static void spin_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned len)
{
    SpinState *s = opaque;
    int env_idx = addr / sizeof(SpinInfo);
    CPUState *cpu = s->cpu[env_idx];
    SpinInfo *curspin = &s->spin[env_idx];
    uint8_t *curspin_p = (uint8_t*)curspin;

    if (cpu->cpu_index == 0) {
        /* primary CPU doesn't spin */
        return;
    }

    curspin_p = &curspin_p[addr % sizeof(SpinInfo)];
    switch (len) {
    case 1:
        stb_p(curspin_p, value);
        break;
    case 2:
        stw_p(curspin_p, value);
        break;
    case 4:
        stl_p(curspin_p, value);
        break;
    }

    if (!(ldq_p(&curspin->addr) & 1)) {
        /* run CPU */
        run_on_cpu(cpu, spin_kick, RUN_ON_CPU_HOST_PTR(curspin));
    }
}

static uint64_t spin_read(void *opaque, hwaddr addr, unsigned len)
{
    SpinState *s = opaque;
    uint8_t *spin_p = &((uint8_t*)s->spin)[addr];

    switch (len) {
    case 1:
        return ldub_p(spin_p);
    case 2:
        return lduw_p(spin_p);
    case 4:
        return ldl_p(spin_p);
    default:
        hw_error("ppce500: unexpected %s with len = %u", __func__, len);
    }
}

static const MemoryRegionOps spin_rw_ops = {
    .read = spin_read,
    .write = spin_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void ppce500_spin_initfn(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    SpinState *s = E500_SPIN(dev);

    memory_region_init_io(&s->iomem, obj, &spin_rw_ops, s,
                          "e500 spin pv device", sizeof(SpinInfo) * MAX_CPUS);
    sysbus_init_mmio(dev, &s->iomem);
}

static void ppce500_spin_realize(DeviceState *dev, Error **errp)
{
    SpinState *s = E500_SPIN(dev);

    if (s->cpu_count == 0) {
        error_setg(errp, "'cpus-qom-path' property array must be set");
        return;
    } else if (s->cpu_count > MAX_CPUS) {
        error_setg(errp, "at most %d CPUs are supported", MAX_CPUS);
        return;
    }

    s->cpu = g_new(CPUState *, s->cpu_count);
    for (unsigned i = 0; i < s->cpu_count; i++) {
        bool ambiguous;
        Object *obj;

        obj = object_resolve_path(s->cpu_canonical_path[i], &ambiguous);
        assert(!ambiguous);
        s->cpu[i] = CPU(obj);
    }
}

static Property ppce500_spin_properties[] = {
    DEFINE_PROP_ARRAY("cpus-qom-path", SpinState, cpu_count,
                      cpu_canonical_path, qdev_prop_string, char *),
    DEFINE_PROP_END_OF_LIST(),
};

static void ppce500_spin_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = spin_reset;
    dc->realize = ppce500_spin_realize;
    device_class_set_props(dc, ppce500_spin_properties);
}

static const TypeInfo ppce500_spin_types[] = {
    {
        .name           = TYPE_E500_SPIN,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(SpinState),
        .instance_init  = ppce500_spin_initfn,
        .class_init     = ppce500_spin_class_init,
    },
};

DEFINE_TYPES(ppce500_spin_types)
