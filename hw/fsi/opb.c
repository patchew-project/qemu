/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM On-chip Peripheral Bus
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"

#include "hw/fsi/opb.h"

uint8_t fsi_opb_read8(OPBus *opb, hwaddr addr)
{
    MemTxResult tx;
    uint8_t data;

    tx = address_space_read(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, &data,
                            sizeof(data));
    if (tx) {
        trace_fsi_opb_read8(addr, sizeof(data));
    }

    return data;
}

uint16_t fsi_opb_read16(OPBus *opb, hwaddr addr)
{
    MemTxResult tx;
    uint16_t data;

    tx = address_space_read(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, &data,
                            sizeof(data));
    if (tx) {
        trace_fsi_opb_read16(addr, sizeof(data));
    }

    return data;
}

uint32_t fsi_opb_read32(OPBus *opb, hwaddr addr)
{
    MemTxResult tx;
    uint32_t data;

    tx = address_space_read(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, &data,
                            sizeof(data));
    if (tx) {
        trace_fsi_opb_read32(addr, sizeof(data));
    }

    return data;
}

void fsi_opb_write8(OPBus *opb, hwaddr addr, uint8_t data)
{
    MemTxResult tx;

    tx = address_space_write(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, &data,
                             sizeof(data));
    if (tx) {
        trace_fsi_opb_write8(addr, sizeof(data));
    }
}

void fsi_opb_write16(OPBus *opb, hwaddr addr, uint16_t data)
{
    MemTxResult tx;

    tx = address_space_write(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, &data,
                             sizeof(data));
    if (tx) {
        trace_fsi_opb_write16(addr, sizeof(data));
    }
}

void fsi_opb_write32(OPBus *opb, hwaddr addr, uint32_t data)
{
    MemTxResult tx;

    tx = address_space_write(&opb->as, addr, MEMTXATTRS_UNSPECIFIED, &data,
                             sizeof(data));
    if (tx) {
        trace_fsi_opb_write32(addr, sizeof(data));
    }
}

void fsi_opb_fsi_master_address(OPBus *opb, hwaddr addr)
{
    memory_region_transaction_begin();
    memory_region_set_address(&opb->fsi.iomem, addr);
    memory_region_transaction_commit();
}

void fsi_opb_opb2fsi_address(OPBus *opb, hwaddr addr)
{
    memory_region_transaction_begin();
    memory_region_set_address(&opb->fsi.opb2fsi, addr);
    memory_region_transaction_commit();
}

static void fsi_opb_realize(BusState *bus, Error **errp)
{
    OPBus *opb = OP_BUS(bus);

    memory_region_init_io(&opb->mr, OBJECT(opb), NULL, opb,
                          NULL, UINT32_MAX);
    address_space_init(&opb->as, &opb->mr, "opb");

    if (!object_property_set_bool(OBJECT(&opb->fsi), "realized", true, errp)) {
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

static void fsi_opb_init(Object *o)
{
    OPBus *opb = OP_BUS(o);

    object_initialize_child(o, "fsi-master", &opb->fsi, TYPE_FSI_MASTER);
    qdev_set_parent_bus(DEVICE(&opb->fsi), BUS(o), &error_abort);
}

static void fsi_opb_class_init(ObjectClass *klass, void *data)
{
    BusClass *bc = BUS_CLASS(klass);
    bc->realize = fsi_opb_realize;
}

static const TypeInfo opb_info = {
    .name = TYPE_OP_BUS,
    .parent = TYPE_BUS,
    .instance_init = fsi_opb_init,
    .instance_size = sizeof(OPBus),
    .class_init = fsi_opb_class_init,
    .class_size = sizeof(OPBusClass),
};

static void fsi_opb_register_types(void)
{
    type_register_static(&opb_info);
}

type_init(fsi_opb_register_types);
