/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Common FRU Access Macro
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/fsi/bits.h"
#include "hw/fsi/cfam.h"
#include "hw/fsi/fsi.h"
#include "hw/fsi/engine-scratchpad.h"

#include "hw/qdev-properties.h"

#define TO_REG(x)                          ((x) >> 2)

#define CFAM_ENGINE_CONFIG                  TO_REG(0x04)

#define CFAM_CONFIG_CHIP_ID                TO_REG(0x00)
#define CFAM_CONFIG_CHIP_ID_P9             0xc0022d15
#define   CFAM_CONFIG_CHIP_ID_BREAK        0xc0de0000

static uint64_t cfam_config_read(void *opaque, hwaddr addr, unsigned size)
{
    CFAMConfig *config;
    CFAMState *cfam;
    LBusNode *node;
    int i;

    config = CFAM_CONFIG(opaque);
    cfam = container_of(config, CFAMState, config);

    qemu_log_mask(LOG_UNIMP, "%s: read @0x%" HWADDR_PRIx " size=%d\n",
                  __func__, addr, size);

    assert(size == 4);
    assert(!(addr & 3));

    switch (addr) {
    case 0x00:
        return CFAM_CONFIG_CHIP_ID_P9;
    case 0x04:
        return ENGINE_CONFIG_NEXT
            | 0x00010000                    /* slots */
            | 0x00001000                    /* version */
            | ENGINE_CONFIG_TYPE_PEEK   /* type */
            | 0x0000000c;                   /* crc */
    case 0x08:
        return ENGINE_CONFIG_NEXT
            | 0x00010000                    /* slots */
            | 0x00005000                    /* version */
            | ENGINE_CONFIG_TYPE_FSI    /* type */
            | 0x0000000a;                   /* crc */
        break;
    default:
        /* FIXME: Improve this */
        i = 0xc;
        QLIST_FOREACH(node, &cfam->lbus.devices, next) {
            if (i == addr) {
                return LBUS_DEVICE_GET_CLASS(node->ldev)->config;
            }
            i += size;
        }

        if (i == addr) {
            return 0;
        }

        return 0xc0de0000;
    }
}

static void cfam_config_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    CFAMConfig *s = CFAM_CONFIG(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: write @0x%" HWADDR_PRIx " size=%d "
                  "value=%"PRIx64"\n", __func__, addr, size, data);

    assert(size == 4);
    assert(!(addr & 3));

    switch (TO_REG(addr)) {
    case CFAM_CONFIG_CHIP_ID:
    case CFAM_CONFIG_CHIP_ID + 4:
        if (data == CFAM_CONFIG_CHIP_ID_BREAK) {
            bus_cold_reset(qdev_get_parent_bus(DEVICE(s)));
        }
    break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Not implemented: 0x%"
                      HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
    }
}

static const struct MemoryRegionOps cfam_config_ops = {
    .read = cfam_config_read,
    .write = cfam_config_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void cfam_config_realize(DeviceState *dev, Error **errp)
{
    CFAMConfig *s = CFAM_CONFIG(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &cfam_config_ops, s,
                          TYPE_CFAM_CONFIG, 0x400);
}

static void cfam_config_reset(DeviceState *dev)
{
    /* Config is read-only */
}

static void cfam_config_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->bus_type = TYPE_LBUS;
    dc->realize = cfam_config_realize;
    dc->reset = cfam_config_reset;
}

static const TypeInfo cfam_config_info = {
    .name = TYPE_CFAM_CONFIG,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CFAMConfig),
    .class_init = cfam_config_class_init,
};

static uint64_t cfam_unimplemented_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: read @0x%" HWADDR_PRIx " size=%d\n",
                  __func__, addr, size);

    return 0;
}

static void cfam_unimplemented_write(void *opaque, hwaddr addr, uint64_t data,
                                     unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: write @0x%" HWADDR_PRIx " size=%d "
                  "value=%"PRIx64"\n", __func__, addr, size, data);
}

static const struct MemoryRegionOps cfam_unimplemented_ops = {
    .read = cfam_unimplemented_read,
    .write = cfam_unimplemented_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void cfam_realize(DeviceState *dev, Error **errp)
{
    CFAMState *cfam = CFAM(dev);
    FSISlaveState *slave = FSI_SLAVE(dev);
    Error *err = NULL;

    /* Each slave has a 2MiB address space */
    memory_region_init_io(&cfam->mr, OBJECT(cfam), &cfam_unimplemented_ops,
                          cfam, TYPE_CFAM, 2 * 1024 * 1024);
    address_space_init(&cfam->as, &cfam->mr, TYPE_CFAM);

    qbus_init(&cfam->lbus, sizeof(cfam->lbus), TYPE_LBUS,
                        DEVICE(cfam), NULL);

    lbus_create_device(&cfam->lbus, TYPE_SCRATCHPAD, 0);

    object_property_set_bool(OBJECT(&cfam->config), "realized", true, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    qdev_set_parent_bus(DEVICE(&cfam->config), BUS(&cfam->lbus), &error_abort);

    object_property_set_bool(OBJECT(&cfam->lbus), "realized", true, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&cfam->mr, 0, &cfam->config.iomem);
    /* memory_region_add_subregion(&cfam->mr, 0x800, &cfam->lbus.peek.iomem); */
    memory_region_add_subregion(&cfam->mr, 0x800, &slave->iomem);
    memory_region_add_subregion(&cfam->mr, 0xc00, &cfam->lbus.mr);
}

static void cfam_reset(DeviceState *dev)
{
    /* TODO: Figure out how inherited reset works */
}

static void cfam_init(Object *o)
{
    CFAMState *s = CFAM(o);

    object_initialize_child(o, TYPE_CFAM_CONFIG, &s->config, TYPE_CFAM_CONFIG);
}

static void cfam_finalize(Object *o)
{
    CFAMState *s = CFAM(o);

    address_space_destroy(&s->as);
}

static void cfam_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->bus_type = TYPE_FSI_BUS;
    dc->realize = cfam_realize;
    dc->reset = cfam_reset;
}

static const TypeInfo cfam_info = {
    .name = TYPE_CFAM,
    .parent = TYPE_FSI_SLAVE,
    .instance_init = cfam_init,
    .instance_finalize = cfam_finalize,
    .instance_size = sizeof(CFAMState),
    .class_init = cfam_class_init,
};

static void cfam_register_types(void)
{
    type_register_static(&cfam_config_info);
    type_register_static(&cfam_info);
}

type_init(cfam_register_types);
