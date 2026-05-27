/*
 * Specific Purpose Memory (SPM) device
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * Authors:
 *  FangSheng Huang <FangSheng.Huang@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "hw/mem/spm-memory.h"
#include "hw/mem/memory-device.h"
#include "hw/i386/e820_memory_layout.h"
#include "migration/vmstate.h"
#include "system/hostmem.h"
#include "system/numa.h"
#include "system/system.h"

static QLIST_HEAD(, SpmMemoryDevice) spm_memory_list =
    QLIST_HEAD_INITIALIZER(spm_memory_list);
static Notifier spm_machine_done_notifier;
static bool spm_machine_done_registered;

#define SPM_MEMORY_MEMDEV_PROP    "memdev"
#define SPM_MEMORY_NODE_PROP      "node"
#define SPM_MEMORY_ADDR_PROP      "addr"

static const Property spm_memory_properties[] = {
    DEFINE_PROP_LINK(SPM_MEMORY_MEMDEV_PROP, SpmMemoryDevice, hostmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_UINT32(SPM_MEMORY_NODE_PROP, SpmMemoryDevice, node, 0),
    DEFINE_PROP_UINT64(SPM_MEMORY_ADDR_PROP, SpmMemoryDevice, addr, 0),
};

static uint64_t spm_memory_md_get_addr(const MemoryDeviceState *md)
{
    return SPM_MEMORY(md)->addr;
}

static void spm_memory_md_set_addr(MemoryDeviceState *md, uint64_t addr,
                                   Error **errp)
{
    SPM_MEMORY(md)->addr = addr;
}

static MemoryRegion *spm_memory_md_get_memory_region(MemoryDeviceState *md,
                                                    Error **errp)
{
    SpmMemoryDevice *spm = SPM_MEMORY(md);

    if (!spm->hostmem) {
        error_setg(errp, "'memdev' property must be set");
        return NULL;
    }
    return host_memory_backend_get_memory(spm->hostmem);
}

static uint64_t spm_memory_md_get_plugged_size(const MemoryDeviceState *md,
                                               Error **errp)
{
    SpmMemoryDevice *spm = SPM_MEMORY(md);
    return spm->hostmem ?
        memory_region_size(host_memory_backend_get_memory(spm->hostmem)) : 0;
}

static void spm_memory_md_fill_device_info(const MemoryDeviceState *md,
                                           MemoryDeviceInfo *info)
{
    SpmMemoryDeviceInfo *di = g_new0(SpmMemoryDeviceInfo, 1);
    SpmMemoryDevice *spm = SPM_MEMORY(md);
    DeviceState *dev = DEVICE(md);

    di->id = dev->id ? g_strdup(dev->id) : NULL;
    di->memaddr = spm->addr;
    di->size = spm->hostmem ? memory_region_size(
                   host_memory_backend_get_memory(spm->hostmem)) : 0;
    di->node = spm->node;
    di->memdev = spm->hostmem ?
                 object_get_canonical_path(OBJECT(spm->hostmem)) : NULL;

    info->u.spm_memory.data = di;
    info->type = MEMORY_DEVICE_INFO_KIND_SPM_MEMORY;
}

typedef struct {
    uint32_t node_id;
    const SpmMemoryDevice *self;  /* exclude self when walking */
    bool conflict;
} SpmNodeCheckCtx;

static int spm_check_node_collision_cb(Object *obj, void *opaque)
{
    SpmNodeCheckCtx *ctx = opaque;
    uint32_t other_node;

    if (!object_dynamic_cast(obj, TYPE_MEMORY_DEVICE)) {
        return 0;
    }
    /*
     * Skip self.  Compare canonical Object* pointers, not interface-cast
     * MemoryDeviceState* (different address under INTERFACE_CHECK).
     */
    if (obj == OBJECT(ctx->self)) {
        return 0;
    }

    /*
     * Not all memory-device subclasses have a "node" property; skip
     * those silently rather than asserting.
     */
    if (!object_property_find(obj, "node")) {
        return 0;
    }
    other_node = (uint32_t)object_property_get_uint(obj, "node", NULL);
    if (other_node == ctx->node_id) {
        ctx->conflict = true;
        return 1; /* stop walk */
    }
    return 0;
}

/*
 * Require the target NUMA node to be SPM-only: driver-side discovery
 * uses proximity_domain as the key, so a node mixing SPM with other
 * memory yields ambiguous discovery.
 */
static void spm_memory_check_node_exclusive(SpmMemoryDevice *spm,
                                            MachineState *ms, Error **errp)
{
    ERRP_GUARD();
    SpmNodeCheckCtx ctx = { spm->node, spm, false };

    /* Bounds check: spm->node must be a valid NUMA node id */
    if (!ms->numa_state || spm->node >= ms->numa_state->num_nodes) {
        error_setg(errp,
                   "spm-memory: node %u out of range "
                   "(numa_state has %d nodes)", spm->node,
                   ms->numa_state ? ms->numa_state->num_nodes : 0);
        return;
    }

    /* Check 1: target node must not have memory from -numa node,memdev= */
    if (ms->numa_state->nodes[spm->node].node_mem > 0) {
        error_setg(errp,
                   "spm-memory: NUMA node %u already has memory attached "
                   "via -numa node,memdev=; SPM nodes must be SPM-only",
                   spm->node);
        return;
    }

    /* Check 2: target node must not already have another memory device */
    object_child_foreach_recursive(qdev_get_machine(),
                                   spm_check_node_collision_cb, &ctx);
    if (ctx.conflict) {
        error_setg(errp,
                   "spm-memory: NUMA node %u already has another memory "
                   "device plugged; SPM nodes must be SPM-only", spm->node);
        return;
    }
}

static void spm_memory_machine_done(Notifier *n, void *opaque)
{
    SpmMemoryDevice *spm;
    MemoryDeviceClass *mdc;
    uint64_t addr, size;

    QLIST_FOREACH(spm, &spm_memory_list, next) {
        g_assert(spm->hostmem);
        mdc = MEMORY_DEVICE_GET_CLASS(MEMORY_DEVICE(spm));
        addr = mdc->get_addr(MEMORY_DEVICE(spm));
        size = memory_region_size(
                   host_memory_backend_get_memory(spm->hostmem));
        e820_add_entry(addr, size, E820_SOFT_RESERVED);
    }
}

static void spm_memory_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    SpmMemoryDevice *spm = SPM_MEMORY(dev);
    MachineState *ms = MACHINE(qdev_get_machine());

    if (phase_check(PHASE_MACHINE_READY)) {
        error_setg(errp, "spm-memory: hotplug is not supported "
                         "(boot-time-only device)");
        return;
    }

    if (!spm->hostmem) {
        error_setg(errp, "'%s' property is required", SPM_MEMORY_MEMDEV_PROP);
        return;
    }
    if (host_memory_backend_is_mapped(spm->hostmem)) {
        error_setg(errp, "memory backend '%s' is already in use",
                   object_get_canonical_path_component(OBJECT(spm->hostmem)));
        return;
    }

    spm_memory_check_node_exclusive(spm, ms, errp);
    if (*errp) {
        return;
    }

    memory_device_pre_plug(MEMORY_DEVICE(spm), ms, errp);
    if (*errp) {
        return;
    }

    host_memory_backend_set_mapped(spm->hostmem, true);
    memory_device_plug(MEMORY_DEVICE(spm), ms);

    QLIST_INSERT_HEAD(&spm_memory_list, spm, next);

    if (!spm_machine_done_registered) {
        spm_machine_done_notifier.notify = spm_memory_machine_done;
        qemu_add_machine_init_done_notifier(&spm_machine_done_notifier);
        spm_machine_done_registered = true;
    }
}

static const VMStateDescription vmstate_spm_memory = {
    .name = TYPE_SPM_MEMORY,
    .unmigratable = 1,
};

static void spm_memory_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(oc);

    dc->desc = "SPM (Specific Purpose Memory) device";
    dc->hotpluggable = false;
    dc->realize = spm_memory_realize;
    dc->vmsd = &vmstate_spm_memory;
    device_class_set_props(dc, spm_memory_properties);

    mdc->get_addr            = spm_memory_md_get_addr;
    mdc->set_addr            = spm_memory_md_set_addr;
    mdc->get_memory_region   = spm_memory_md_get_memory_region;
    mdc->get_plugged_size    = spm_memory_md_get_plugged_size;
    mdc->fill_device_info    = spm_memory_md_fill_device_info;
}

static const TypeInfo spm_memory_info = {
    .name          = TYPE_SPM_MEMORY,
    .parent        = TYPE_DEVICE,
    .class_size    = sizeof(SpmMemoryDeviceClass),
    .class_init    = spm_memory_class_init,
    .instance_size = sizeof(SpmMemoryDevice),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void spm_memory_register_types(void)
{
    type_register_static(&spm_memory_info);
}

type_init(spm_memory_register_types)
