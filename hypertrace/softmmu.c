/*
 * QEMU-side management of hypertrace in softmmu emulation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Implementation details
 * ======================
 *
 * There are 3 channels, each a BAR of a virtual device that can be used through
 * MMIO.
 *
 *
 * - Configuration channel: Exposes configuration parameters.
 *
 * - Data channel: Lets guests write argument values. Each guest client should
 *   use a different offset to avoid concurrency problems.
 *
 * - Control channel: Triggers the hypertrace event on a write, providing the
 *   first argument. Offset in the control channel sets the offset in the data
 *   channel.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hypertrace/common.h"
#include "hypertrace/trace.h"
#include "hw/pci/pci.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/error-report.h"


typedef struct HypertraceState {
    PCIDevice dev;

    uint64_t max_clients;
    struct hypertrace_config hconfig;

    MemoryRegion config;
    void *config_ptr;
    MemoryRegion data;
    void *data_ptr;
    MemoryRegion control;
    void *control_ptr;

    Error *migration_blocker;
} HypertraceState;


static uint64_t hypertrace_control_io_read(void *opaque, hwaddr addr,
                                           unsigned size)
{
    uint64_t res;
    HypertraceState *s = opaque;
    char *mem = &((char *)s->control_ptr)[addr];

    switch (size) {
    case 1:
    {
        res = ((uint8_t *)mem)[0];
        break;
    }
    case 2:
    {
        res = ((uint16_t *)mem)[0];
        break;
    }
    case 4:
    {
        res = ((uint32_t *)mem)[0];
        break;
    }
    case 8:
    {
        res = ((uint64_t *)mem)[0];
        break;
    }
    default:
        error_report("error: hypertrace: Unexpected read of size %d", size);
        abort();
    }

    return res;
}

static void hypertrace_control_io_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    HypertraceState *s = opaque;
    char *mem = &((char *)s->control_ptr)[addr];

    switch (size) {
    case 1:
    {
        uint8_t *res = (uint8_t *)mem;
        *res = (uint8_t)data;
        break;
    }
    case 2:
    {
        uint16_t *res = (uint16_t *)mem;
        *res = (uint16_t)data;
        break;
    }
    case 4:
    {
        uint32_t *res = (uint32_t *)mem;
        *res = (uint32_t)data;
        break;
    }
    case 8:
    {
        uint64_t *res = (uint64_t *)mem;
        *res = (uint64_t)data;
        break;
    }
    default:
        error_report("error: hypertrace: Unexpected write of size %d", size);
        abort();
    }

    if ((addr + size) % sizeof(uint64_t) == 0) {
        uint64_t client = addr / sizeof(uint64_t);
        uint64_t vcontrol = ((uint64_t *)s->control_ptr)[client];
        uint64_t *data_ptr = (uint64_t *)s->data_ptr;
        data_ptr = &data_ptr[client * s->hconfig.client_data_size];
        hypertrace_emit(current_cpu, vcontrol, data_ptr);
    }
}

static const MemoryRegionOps hypertrace_control_ops = {
    .read = &hypertrace_control_io_read,
    .write = &hypertrace_control_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};


static void hypertrace_realize(PCIDevice *dev, Error **errp)
{
    Error *local_err = NULL;
    struct hypertrace_config *config;
    HypertraceState *s = DO_UPCAST(HypertraceState, dev, dev);
    Error *err = NULL;

    if (s->max_clients < 1) {
        error_setg(errp, "hypertrace: must have at least one client");
        return;
    }

    hypertrace_init_config(&s->hconfig, s->max_clients);

    error_setg(&s->migration_blocker,
               "The 'hypertrace' device cannot be migrated");
    migrate_add_blocker(s->migration_blocker, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_free(s->migration_blocker);
        return;
    }

    pci_set_word(s->dev.config + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    /* config channel */
    memory_region_init_ram(&s->config, OBJECT(s), "hypertrace.config",
                           TARGET_PAGE_SIZE, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->config);
    s->config_ptr = qemu_map_ram_ptr(s->config.ram_block, 0);
    config = s->config_ptr;
    config->max_clients = tswap64(s->hconfig.max_clients);
    config->client_args = tswap64(s->hconfig.client_args);
    config->client_data_size = tswap64(s->hconfig.client_data_size);
    config->control_size = tswap64(s->hconfig.control_size);
    config->data_size = tswap64(s->hconfig.data_size);

    /* data channel */
    memory_region_init_ram(&s->data, OBJECT(s), "hypertrace.data",
                           s->hconfig.data_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    pci_register_bar(&s->dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->data);
    s->data_ptr = qemu_map_ram_ptr(s->data.ram_block, 0);

    /* control channel */
    memory_region_init_io(&s->control, OBJECT(s), &hypertrace_control_ops, s,
                          "hypertrace.control", s->hconfig.control_size);
    pci_register_bar(&s->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->control);
    s->control_ptr = qemu_map_ram_ptr(s->control.ram_block, 0);
}


static Property hypertrace_properties[] = {
    DEFINE_PROP_UINT64("max-clients", HypertraceState, max_clients, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void hypertrace_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = hypertrace_realize;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_HYPERTRACE;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    dc->desc  = "Hypertrace communication channel",
    dc->props = hypertrace_properties;
}

static TypeInfo hypertrace_info = {
    .name          = "hypertrace",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(HypertraceState),
    .class_init    = hypertrace_class_init,
};

static void hypertrace_register_types(void)
{
    type_register_static(&hypertrace_info);
}

type_init(hypertrace_register_types)
