/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM On-chip Peripheral Bus
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/fsi/opb.h"

static MemTxResult opb_read(OPBus *opb, hwaddr addr, void *data, size_t len)
{
    return address_space_read(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                              len);
}

uint8_t opb_read8(OPBus *opb, hwaddr addr)
{
    MemTxResult tx;
    uint8_t data;

    tx = opb_read(opb, addr, &data, sizeof(data));
    /* FIXME: improve error handling */
    assert(!tx);

    return data;
}

uint16_t opb_read16(OPBus *opb, hwaddr addr)
{
    MemTxResult tx;
    uint16_t data;

    tx = opb_read(opb, addr, &data, sizeof(data));
    /* FIXME: improve error handling */
    assert(!tx);

    return data;
}

uint32_t opb_read32(OPBus *opb, hwaddr addr)
{
    MemTxResult tx;
    uint32_t data;

    tx = opb_read(opb, addr, &data, sizeof(data));
    /* FIXME: improve error handling */
    assert(!tx);

    return data;
}

static MemTxResult opb_write(OPBus *opb, hwaddr addr, void *data, size_t len)
{
    return address_space_write(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                               len);
}

void opb_write8(OPBus *opb, hwaddr addr, uint8_t data)
{
    MemTxResult tx;

    tx = opb_write(opb, addr, &data, sizeof(data));
    /* FIXME: improve error handling */
    assert(!tx);
}

void opb_write16(OPBus *opb, hwaddr addr, uint16_t data)
{
    MemTxResult tx;

    tx = opb_write(opb, addr, &data, sizeof(data));
    /* FIXME: improve error handling */
    assert(!tx);
}

void opb_write32(OPBus *opb, hwaddr addr, uint32_t data)
{
    MemTxResult tx;

    tx = opb_write(opb, addr, &data, sizeof(data));
    /* FIXME: improve error handling */
    assert(!tx);
}

void opb_fsi_master_address(OPBus *opb, hwaddr addr)
{
    memory_region_transaction_begin();
    memory_region_set_address(&opb->fsi.iomem, addr);
    memory_region_transaction_commit();
}

void opb_opb2fsi_address(OPBus *opb, hwaddr addr)
{
    memory_region_transaction_begin();
    memory_region_set_address(&opb->fsi.opb2fsi, addr);
    memory_region_transaction_commit();
}

static uint64_t opb_unimplemented_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: read @0x%" HWADDR_PRIx " size=%d\n",
                  __func__, addr, size);

    return 0;
}

static void opb_unimplemented_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: write @0x%" HWADDR_PRIx " size=%d "
                  "value=%"PRIx64"\n", __func__, addr, size, data);
}

static const struct MemoryRegionOps opb_unimplemented_ops = {
    .read = opb_unimplemented_read,
    .write = opb_unimplemented_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void opb_realize(BusState *bus, Error **errp)
{
    OPBus *opb = OP_BUS(bus);
    Error *err = NULL;

    memory_region_init_io(&opb->mr, OBJECT(opb), &opb_unimplemented_ops, opb,
                          NULL, UINT32_MAX);
    address_space_init(&opb->as, &opb->mr, "opb");

    object_property_set_bool(OBJECT(&opb->fsi), "realized", true, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(&opb->mr, 0x80000000, &opb->fsi.iomem);

    /* OPB2FSI region */
    /*
     * Avoid endianness issues by mapping each slave's memory region directly.
     * Manually bridging multiple address-spaces causes endian swapping
     * headaches as memory_region_dispatch_read() and
     * memory_region_dispatch_write() correct the endianness based on the
     * target machine endianness and not relative to the device endianness on
     * either side of the bridge.
     */
    /*
     * XXX: This is a bit hairy and will need to be fixed when I sort out the
     * bus/slave relationship and any changes to the CFAM modelling (multiple
     * slaves, LBUS)
     */
    memory_region_add_subregion(&opb->mr, 0xa0000000, &opb->fsi.opb2fsi);
}

static void opb_init(Object *o)
{
    OPBus *opb = OP_BUS(o);

    object_initialize_child(o, "fsi-master", &opb->fsi, TYPE_FSI_MASTER);
    qdev_set_parent_bus(DEVICE(&opb->fsi), BUS(o), &error_abort);
}

static void opb_finalize(Object *o)
{
    OPBus *opb = OP_BUS(o);

    address_space_destroy(&opb->as);
}

static void opb_class_init(ObjectClass *klass, void *data)
{
    BusClass *bc = BUS_CLASS(klass);
    bc->realize = opb_realize;
}

static const TypeInfo opb_info = {
    .name = TYPE_OP_BUS,
    .parent = TYPE_BUS,
    .instance_init = opb_init,
    .instance_finalize = opb_finalize,
    .instance_size = sizeof(OPBus),
    .class_init = opb_class_init,
    .class_size = sizeof(OPBusClass),
};

static void opb_register_types(void)
{
    type_register_static(&opb_info);
}

type_init(opb_register_types);
