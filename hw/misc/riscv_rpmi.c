/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI transport device.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/riscv_rpmi.h"
#include "hw/misc/riscv_rpmi_internal.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/physmem.h"
#include "system/runstate.h"
#include "librpmi.h"
#include "librpmi_env.h"

void *rpmi_env_zalloc(rpmi_size_t size)
{
    return g_malloc0(size);
}

void rpmi_env_free(void *ptr)
{
    g_free(ptr);
}

void rpmi_env_writel(rpmi_uint64_t addr, rpmi_uint32_t val)
{
    uint32_t val_le;

    stl_le_p(&val_le, val);
    physical_memory_write(addr, &val_le, sizeof(val_le));
}

static bool riscv_rpmi_addr_in_range(uint64_t addr, uint32_t len,
                                     uint64_t base, uint64_t size)
{
    return len && size >= len && addr >= base && addr - base <= size - len;
}

static enum rpmi_error shmem_qemu_read(void *priv, rpmi_uint64_t addr,
                                       void *buf, rpmi_uint32_t len)
{
    RiscvRpmiState *s = RISCV_RPMI(priv);

    if (!s || !buf ||
        !riscv_rpmi_addr_in_range(addr, len, s->shmem_base,
                                  s->shmem_size)) {
        return RPMI_ERR_BAD_RANGE;
    }

    physical_memory_read(addr, buf, len);
    return RPMI_SUCCESS;
}

static enum rpmi_error shmem_qemu_write(void *priv, rpmi_uint64_t addr,
                                       const void *buf, rpmi_uint32_t len)
{
    RiscvRpmiState *s = RISCV_RPMI(priv);

    if (!s || !buf ||
        !riscv_rpmi_addr_in_range(addr, len, s->shmem_base,
                                  s->shmem_size)) {
        return RPMI_ERR_BAD_RANGE;
    }

    physical_memory_write(addr, buf, len);
    return RPMI_SUCCESS;
}

static enum rpmi_error shmem_qemu_fill(void *priv, rpmi_uint64_t addr,
                                       char ch, rpmi_uint32_t len)
{
    RiscvRpmiState *s = RISCV_RPMI(priv);

    if (!s ||
        !riscv_rpmi_addr_in_range(addr, len, s->shmem_base,
                                  s->shmem_size)) {
        return RPMI_ERR_BAD_RANGE;
    }

    while (len--) {
        physical_memory_write(addr++, &ch, sizeof(ch));
    }

    return RPMI_SUCCESS;
}

const struct rpmi_shmem_platform_ops rpmi_shmem_qemu_ops = {
    .read = shmem_qemu_read,
    .write = shmem_qemu_write,
    .fill = shmem_qemu_fill,
};

static uint64_t riscv_rpmi_read(void *opaque, hwaddr offset, unsigned int size)
{
    RiscvRpmiState *s = opaque;

    if (offset != 0 || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, offset, size);
        return 0;
    }

    return s->doorbell;
}

static bool riscv_rpmi_transport_indices_valid(RiscvRpmiState *s);

static void riscv_rpmi_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    RiscvRpmiState *s = opaque;

    if (offset != 0 || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, offset, size);
        return;
    }

    s->doorbell = value;

    if (value == 1) {
        if (s->context && riscv_rpmi_transport_indices_valid(s)) {
            rpmi_context_process_a2p_request(s->context);
            rpmi_context_process_all_events(s->context);
        }
        s->doorbell = 0;
    }
}

static const MemoryRegionOps riscv_rpmi_ops = {
    .read = riscv_rpmi_read,
    .write = riscv_rpmi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool riscv_rpmi_queue_indices_valid(RiscvRpmiState *s,
                                           uint64_t queue_base,
                                           uint32_t queue_size)
{
    uint8_t *shmem = memory_region_get_ram_ptr(&s->shmem);
    uint32_t data_slots = queue_size / RPMI_QUEUE_SLOT_SIZE - 2;
    uint32_t head;
    uint32_t tail;

    head = ldl_le_p(shmem + queue_base);
    tail = ldl_le_p(shmem + queue_base + RPMI_QUEUE_SLOT_SIZE);
    if (head >= data_slots || tail >= data_slots) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid queue indices head %u tail %u slots %u\n",
                      __func__, head, tail, data_slots);
        return false;
    }

    return true;
}

static bool riscv_rpmi_transport_indices_valid(RiscvRpmiState *s)
{
    if (!s->has_shmem || !s->a2p_req_size) {
        return false;
    }

    if (!riscv_rpmi_queue_indices_valid(s, 0, s->a2p_req_size)) {
        return false;
    }

    return riscv_rpmi_queue_indices_valid(s, s->a2p_req_size,
                                          s->a2p_req_size);
}

typedef struct RiscvRpmiServiceOps {
    enum rpmi_servicegroup_id service_group;
    bool (*add)(RiscvRpmiState *s, Error **errp);
    void (*remove)(RiscvRpmiState *s);
} RiscvRpmiServiceOps;

static const RiscvRpmiServiceOps riscv_rpmi_service_ops[] = {
    {
        .service_group = RPMI_SRVGRP_SYSTEM_RESET,
        .add = riscv_rpmi_sysreset_add,
        .remove = riscv_rpmi_sysreset_remove,
    }, {
        .service_group = RPMI_SRVGRP_HSM,
        .add = riscv_rpmi_hsm_add,
        .remove = riscv_rpmi_hsm_remove,
    }, {
        .service_group = RPMI_SRVGRP_SYSTEM_SUSPEND,
        .add = riscv_rpmi_syssusp_add,
        .remove = riscv_rpmi_syssusp_remove,
    },
};

static const RiscvRpmiServiceOps *riscv_rpmi_service_ops_by_group(
    enum rpmi_servicegroup_id service_group)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(riscv_rpmi_service_ops); i++) {
        if (riscv_rpmi_service_ops[i].service_group == service_group) {
            return &riscv_rpmi_service_ops[i];
        }
    }

    return NULL;
}

static void riscv_rpmi_configure_base(RiscvRpmiState *s,
                                      const RiscvRpmiConfig *cfg)
{
    s->platform_info = g_strdup(cfg->platform_info);
    s->machine_ops = cfg->machine_ops;
    s->services = cfg->services;
    s->service_count = cfg->service_count;

    if (cfg->hart_count) {
        s->hart_count = cfg->hart_count;
        if (cfg->hart_ids) {
            s->hart_ids = g_memdup2(cfg->hart_ids,
                                    cfg->hart_count * sizeof(*cfg->hart_ids));
        }
    }
}

static void riscv_rpmi_init(Object *obj)
{
    RiscvRpmiState *s = RISCV_RPMI(obj);

    memory_region_init_io(&s->mmio, obj, &riscv_rpmi_ops, s,
                          TYPE_RISCV_RPMI, RPMI_DBREG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void riscv_rpmi_reset_hold(Object *obj, ResetType type)
{
    RiscvRpmiState *s = RISCV_RPMI(obj);

    s->doorbell = 0;

    if (s->has_shmem) {
        memset(memory_region_get_ram_ptr(&s->shmem), 0, s->shmem_size);
        memory_region_set_dirty(&s->shmem, 0, s->shmem_size);
    }

    riscv_rpmi_hsm_reset(s);
}

static void riscv_rpmi_cleanup(RiscvRpmiState *s)
{
    for (uint32_t i = ARRAY_SIZE(riscv_rpmi_service_ops); i > 0; i--) {
        riscv_rpmi_service_ops[i - 1].remove(s);
    }

    if (s->context) {
        rpmi_context_destroy(s->context);
        s->context = NULL;
    }

    if (s->transport) {
        rpmi_transport_shmem_destroy(s->transport);
        s->transport = NULL;
    }

    if (s->rpmi_shmem) {
        rpmi_shmem_destroy(s->rpmi_shmem);
        s->rpmi_shmem = NULL;
    }

    if (s->has_shmem) {
        memory_region_del_subregion(get_system_memory(), &s->shmem);
        s->has_shmem = false;
    }
}

bool riscv_rpmi_service_enabled(RiscvRpmiState *s,
                                enum rpmi_servicegroup_id service_group)
{
    uint32_t i;

    for (i = 0; i < s->service_count; i++) {
        if (s->services[i].service_group == service_group) {
            return true;
        }
    }

    return false;
}

bool riscv_rpmi_context_add_group(RiscvRpmiState *s,
                                  struct rpmi_service_group *group,
                                  const char *name,
                                  Error **errp)
{
    enum rpmi_error rc;

    rc = rpmi_context_add_group(s->context, group);
    if (rc != RPMI_SUCCESS) {
        error_setg(errp, "failed to add RPMI %s service group: %d", name, rc);
        return false;
    }

    return true;
}

void riscv_rpmi_context_remove_group(RiscvRpmiState *s,
                                     struct rpmi_service_group *group)
{
    if (s->context && group) {
        rpmi_context_remove_group(s->context, group);
    }
}

static bool riscv_rpmi_validate_config(RiscvRpmiState *s, Error **errp)
{
    uint64_t queue_bytes;

    if (!s->shmem_size) {
        error_setg(errp, "RPMI shared memory size must be non-zero");
        return false;
    }

    if (s->shmem_base & (RPMI_QUEUE_SLOT_SIZE - 1)) {
        error_setg(errp, "RPMI shared memory base must be %u-byte aligned",
                   RPMI_QUEUE_SLOT_SIZE);
        return false;
    }

    if (!s->a2p_req_size) {
        error_setg(errp, "RPMI A2P request queue size must be non-zero");
        return false;
    }

    if (!s->hart_count || !s->hart_ids) {
        error_setg(errp, "RPMI transport requires hart IDs");
        return false;
    }

    for (uint32_t i = 0; i < s->hart_count; i++) {
        for (uint32_t j = i + 1; j < s->hart_count; j++) {
            if (s->hart_ids[i] == s->hart_ids[j]) {
                error_setg(errp, "RPMI hart ID %u is duplicated",
                           s->hart_ids[i]);
                return false;
            }
        }
    }

    if (s->service_count && !s->services) {
        error_setg(errp, "RPMI service count requires service descriptors");
        return false;
    }

    if (s->a2p_req_size % RPMI_QUEUE_SLOT_SIZE) {
        error_setg(errp,
                   "RPMI A2P request queue size must be a multiple of %u",
                   RPMI_QUEUE_SLOT_SIZE);
        return false;
    }

    if (s->p2a_req_size % RPMI_QUEUE_SLOT_SIZE) {
        error_setg(errp,
                   "RPMI P2A request queue size must be a multiple of %u",
                   RPMI_QUEUE_SLOT_SIZE);
        return false;
    }

    queue_bytes = 2 * (uint64_t)s->a2p_req_size +
                  2 * (uint64_t)s->p2a_req_size;
    if (queue_bytes > s->shmem_size) {
        error_setg(errp,
                   "RPMI shared memory size 0x%" PRIx64
                   " is too small for queues 0x%" PRIx64,
                   s->shmem_size, queue_bytes);
        return false;
    }

    return true;
}

static bool riscv_rpmi_add_service_group(RiscvRpmiState *s,
                                         const RiscvRpmiServiceConfig *service,
                                         Error **errp)
{
    const RiscvRpmiServiceOps *ops;

    ops = riscv_rpmi_service_ops_by_group(service->service_group);
    if (!ops) {
        error_setg(errp, "unsupported RPMI service group %u",
                   service->service_group);
        return false;
    }

    return ops->add(s, errp);
}

static bool riscv_rpmi_init_services(RiscvRpmiState *s, Error **errp)
{
    uint32_t i;

    for (i = 0; i < s->service_count; i++) {
        if (!riscv_rpmi_add_service_group(s, &s->services[i], errp)) {
            return false;
        }
    }

    return true;
}

static bool riscv_rpmi_init_context(RiscvRpmiState *s, Error **errp)
{
    const char *platform_info = s->platform_info ?: RPMI_PLAT_INFO;

    s->rpmi_shmem = rpmi_shmem_create("rpmi-shmem", s->shmem_base,
                                      s->shmem_size,
                                      &rpmi_shmem_qemu_ops, s);
    if (!s->rpmi_shmem) {
        error_setg(errp, "failed to create RPMI shared memory backend");
        return false;
    }

    s->transport = rpmi_transport_shmem_create("rpmi-shmem-transport",
                                               RPMI_QUEUE_SLOT_SIZE,
                                               s->a2p_req_size,
                                               s->p2a_req_size,
                                               s->rpmi_shmem);
    if (!s->transport) {
        error_setg(errp, "failed to create RPMI shared memory transport");
        return false;
    }

    s->context = rpmi_context_create("rpmi-virt", s->transport,
                                     RPMI_SRVGRP_ID_MAX_COUNT,
                                     RPMI_PRIVILEGE_M_MODE,
                                     strlen(platform_info) + 1,
                                     platform_info);
    if (!s->context) {
        error_setg(errp, "failed to create RPMI context");
        return false;
    }

    return riscv_rpmi_init_services(s, errp);
}

static void riscv_rpmi_realize(DeviceState *dev, Error **errp)
{
    RiscvRpmiState *s = RISCV_RPMI(dev);
    g_autofree char *name = NULL;

    if (!riscv_rpmi_validate_config(s, errp)) {
        return;
    }

    name = g_strdup_printf("rpmi-shmem@%" PRIx64, s->shmem_base);
    if (!memory_region_init_ram(&s->shmem, OBJECT(dev), name, s->shmem_size,
                                errp)) {
        return;
    }

    memory_region_add_subregion(get_system_memory(), s->shmem_base, &s->shmem);
    s->has_shmem = true;

    if (!riscv_rpmi_init_context(s, errp)) {
        riscv_rpmi_cleanup(s);
    }
}

static void riscv_rpmi_unrealize(DeviceState *dev)
{
    riscv_rpmi_cleanup(RISCV_RPMI(dev));
}

static const VMStateDescription riscv_rpmi_vmstate = {
    .name = TYPE_RISCV_RPMI,
    .unmigratable = 1,
};

static const Property riscv_rpmi_properties[] = {
    DEFINE_PROP_UINT64("shmem-base", RiscvRpmiState, shmem_base, 0),
    DEFINE_PROP_UINT64("shmem-size", RiscvRpmiState, shmem_size, 0),
    DEFINE_PROP_UINT32("a2p-req-size", RiscvRpmiState, a2p_req_size, 0),
    DEFINE_PROP_UINT32("p2a-req-size", RiscvRpmiState, p2a_req_size, 0),
};

static void riscv_rpmi_finalize(Object *obj)
{
    RiscvRpmiState *s = RISCV_RPMI(obj);

    g_free(s->platform_info);
    g_free(s->hart_ids);
}

static void riscv_rpmi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = riscv_rpmi_realize;
    dc->unrealize = riscv_rpmi_unrealize;
    dc->vmsd = &riscv_rpmi_vmstate;
    rc->phases.hold = riscv_rpmi_reset_hold;
    device_class_set_props(dc, riscv_rpmi_properties);
}

static const TypeInfo riscv_rpmi_info = {
    .name          = TYPE_RISCV_RPMI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RiscvRpmiState),
    .instance_init = riscv_rpmi_init,
    .instance_finalize = riscv_rpmi_finalize,
    .class_init    = riscv_rpmi_class_init,
};

static void riscv_rpmi_register_types(void)
{
    type_register_static(&riscv_rpmi_info);
}

type_init(riscv_rpmi_register_types)

DeviceState *riscv_rpmi_create(const RiscvRpmiConfig *cfg, Error **errp)
{
    DeviceState *dev;
    RiscvRpmiState *s;

    if (!cfg) {
        error_setg(errp, "missing RPMI configuration");
        return NULL;
    }

    dev = qdev_new(TYPE_RISCV_RPMI);
    qdev_prop_set_uint64(dev, "shmem-base", cfg->shmem_base);
    qdev_prop_set_uint64(dev, "shmem-size", cfg->shmem_size);
    qdev_prop_set_uint32(dev, "a2p-req-size", cfg->a2p_req_size);
    qdev_prop_set_uint32(dev, "p2a-req-size", cfg->p2a_req_size);

    s = RISCV_RPMI(dev);
    riscv_rpmi_configure_base(s, cfg);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp)) {
        return NULL;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, cfg->doorbell_base);
    return dev;
}
