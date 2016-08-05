/*
 * QEMU-side management of hypertrace in softmmu emulation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Implementation details
 * ======================
 *
 * Both channels are provided as a virtual device with two BARs that can be used
 * through MMIO.
 *
 * Data channel
 * ------------
 *
 * The guest must mmap BAR 1 of the hypertrace virtual device, which will act as
 * regular writable device memory.
 *
 * Regular memory accesses pass data through the data channel.
 *
 * Control channel
 * ---------------
 *
 * The guest must mmap BAR 0 of the hypertrace virtual device.
 *
 * Guest reads from that memory are intercepted by QEMU in order to return the
 * size of the data channel.
 *
 * Guest writes into that memory are intercepted by QEMU in order to raise the
 * "guest_hypertrace" tracing event.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/ram_addr.h"
#include "hw/pci/pci.h"
#include "migration/migration.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "trace.h"


#define PAGE_SIZE TARGET_PAGE_SIZE


typedef struct HypertraceState
{
    PCIDevice dev;

    uint8_t pages;
    uint64_t size;

    union
    {
        uint64_t v;
        char     a[8];
    } c_max_offset;
    union
    {
        uint64_t v;
        char     a[8];
    } c_cmd;

    void *data_ptr;
    MemoryRegion data;
    MemoryRegion control;

    Error *migration_blocker;
} HypertraceState;


static uint64_t hypertrace_control_io_read(void *opaque, hwaddr addr,
                                           unsigned size)
{
    HypertraceState *s = opaque;
    char *mem = &s->c_max_offset.a[addr % sizeof(uint64_t)];

    if (addr + size > sizeof(uint64_t)) {
        error_report("error: hypertrace: Unexpected read to address %lu\n", addr);
        return 0;
    }

    /* control values already have target endianess */

    switch (size) {
    case 1:
    {
        uint8_t *res = (uint8_t*)mem;
        return *res;
    }
    case 2:
    {
        uint16_t *res = (uint16_t*)mem;
        return *res;
    }
    case 4:
    {
        uint32_t *res = (uint32_t*)mem;
        return *res;
    }
    case 8:
    {
        uint64_t *res = (uint64_t*)mem;
        return *res;
    }
    default:
        error_report("error: hypertrace: Unexpected read of size %d\n", size);
        return 0;
    }
}

#include "hypertrace/emit.c"

static void hypertrace_control_io_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    HypertraceState *s = opaque;
    char *mem = &s->c_cmd.a[addr % sizeof(uint64_t)];

    if (addr < sizeof(uint64_t) || addr + size > sizeof(uint64_t) * 2) {
        error_report("error: hypertrace: Unexpected write to address %lu\n", addr);
        return;
    }

    /* c_cmd will have target endianess (left up to the user) */

    switch (size) {
    case 1:
    {
        uint8_t *res = (uint8_t*)mem;
        *res = (uint8_t)data;
        break;
    }
    case 2:
    {
        uint16_t *res = (uint16_t*)mem;
        *res = (uint16_t)data;
        break;
    }
    case 4:
    {
        uint32_t *res = (uint32_t*)mem;
        *res = (uint32_t)data;
        break;
    }
    case 8:
    {
        uint64_t *res = (uint64_t*)mem;
        *res = (uint64_t)data;
        break;
    }
    default:
        error_report("error: hypertrace: Unexpected write of size %d\n", size);
    }

    if ((addr + size) % sizeof(s->c_cmd.v) == 0) {
        uint64_t vcontrol = s->c_cmd.v;
        uint64_t *data_ptr = (uint64_t*)s->data_ptr;
        data_ptr = &data_ptr[vcontrol * CONFIG_HYPERTRACE_ARGS * sizeof(uint64_t)];
        hypertrace_emit(current_cpu, data_ptr);
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
    HypertraceState *s = DO_UPCAST(HypertraceState, dev, dev);
    Error *err = NULL;
    size_t size = s->pages * TARGET_PAGE_SIZE;

    if (s->pages < 1) {
        error_setg(errp, "hypertrace: the data channel must have one or more pages\n");
        return;
    }
    s->c_max_offset.v = tswap64(s->size / (CONFIG_HYPERTRACE_ARGS * sizeof(uint64_t)));

    error_setg(&s->migration_blocker, "The 'hypertrace' device cannot be migrated");
    migrate_add_blocker(s->migration_blocker);

    pci_set_word(s->dev.config + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    /* control channel */
    memory_region_init_io(&s->control, OBJECT(s), &hypertrace_control_ops, s,
                          "hypertrace.control", PAGE_SIZE);
    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->control);

    /* data channel */
    memory_region_init_ram(&s->data, OBJECT(s), "hypertrace.data", size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    pci_register_bar(&s->dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->data);
    s->data_ptr = qemu_map_ram_ptr(s->data.ram_block, 0);
}


static Property hypertrace_properties[] = {
    DEFINE_PROP_UINT8("pages", HypertraceState, pages, 1),
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
