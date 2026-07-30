/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Common FRU Access Macro
 */

#include "qemu/osdep.h"
#include "qemu/units.h"

#include "qapi/error.h"
#include "trace.h"

#include "hw/fsi/cfam.h"
#include "hw/fsi/fsi.h"

#include "hw/core/qdev-properties.h"

#define TO_REG(x)                          ((x) >> 2)

#define CFAM_CONFIG_CHIP_ID                TO_REG(0x00)
#define CFAM_CONFIG_PEEK_STATUS            TO_REG(0x04)
#define CFAM_CONFIG_CHIP_ID_P9             0xc0022d15
#define CFAM_CONFIG_CHIP_ID_BREAK          0xc0de0000

/*
 * Config table of the P9 CFAM: the chip ID followed by one entry per engine,
 * entry n describing the engine at address n * 4. We need to add future
 * engines from address 0x10 onwards.
 */
static const uint32_t cfam_p9_config[] = {
    CFAM_CONFIG_CHIP_ID_P9,
    CFAM_CONFIG_REG(0x1000, ENGINE_CONFIG_TYPE_PEEK, 0xc),
    CFAM_CONFIG_REG(0x5000, ENGINE_CONFIG_TYPE_FSI, 0xa),
    CFAM_CONFIG_REG(0x1000, ENGINE_CONFIG_TYPE_SCRATCHPAD, 0x7),
};

static uint64_t fsi_cfam_config_read(void *opaque, hwaddr addr, unsigned size)
{
    FSICFAMCommonClass *cc = FSI_CFAM_COMMON_GET_CLASS(opaque);
    unsigned int reg = TO_REG(addr);

    trace_fsi_cfam_config_read(addr, size);

    /* Engines past the end of the table are not implemented */
    return reg < cc->config_nr ? cc->config[reg] : 0;
}

static void fsi_cfam_config_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned size)
{
    FSICFAMCommonState *cfam = FSI_CFAM_COMMON(opaque);

    trace_fsi_cfam_config_write(addr, size, data);

    switch (TO_REG(addr)) {
    case CFAM_CONFIG_CHIP_ID:
    case CFAM_CONFIG_PEEK_STATUS:
        if (data == CFAM_CONFIG_CHIP_ID_BREAK) {
            bus_cold_reset(BUS(&cfam->lbus));
        }
        break;
    default:
        trace_fsi_cfam_config_write_noaddr(addr, size, data);
    }
}

static const struct MemoryRegionOps cfam_config_ops = {
    .read = fsi_cfam_config_read,
    .write = fsi_cfam_config_write,
    .valid.max_access_size = 4,
    .valid.min_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t fsi_cfam_unimplemented_read(void *opaque, hwaddr addr,
                                            unsigned size)
{
    trace_fsi_cfam_unimplemented_read(addr, size);

    return 0;
}

static void fsi_cfam_unimplemented_write(void *opaque, hwaddr addr,
                                         uint64_t data, unsigned size)
{
    trace_fsi_cfam_unimplemented_write(addr, size, data);
}

static const struct MemoryRegionOps fsi_cfam_unimplemented_ops = {
    .read = fsi_cfam_unimplemented_read,
    .write = fsi_cfam_unimplemented_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

bool fsi_cfam_add_engine(FSICFAMCommonState *cfam, DeviceState *engine,
                         hwaddr offset, Error **errp)
{
    if (!qdev_realize(engine, BUS(&cfam->lbus), errp)) {
        return false;
    }

    memory_region_add_subregion(&cfam->lbus.mr, offset,
                                &FSI_LBUS_DEVICE(engine)->iomem);
    return true;
}

static void fsi_cfam_common_realize(DeviceState *dev, Error **errp)
{
    FSICFAMCommonState *cfam = FSI_CFAM_COMMON(dev);
    FSICFAMCommonClass *cc = FSI_CFAM_COMMON_GET_CLASS(dev);
    FSISlaveState *slave = FSI_SLAVE(dev);
    const char *type = object_get_typename(OBJECT(dev));
    g_autofree char *config_name = g_strdup_printf("%s.config", type);

    /* Each slave has a 2MiB address space */
    memory_region_init_io(&cfam->mr, OBJECT(cfam), &fsi_cfam_unimplemented_ops,
                          cfam, type, FSI_CFAM_SLOT_SIZE);

    qbus_init(&cfam->lbus, sizeof(cfam->lbus), TYPE_FSI_LBUS, DEVICE(cfam),
              NULL);

    memory_region_init_io(&cfam->config_iomem, OBJECT(cfam), &cfam_config_ops,
                          cfam, config_name, FSI_CFAM_CONFIG_SIZE);

    memory_region_add_subregion(&cfam->mr, 0, &cfam->config_iomem);
    memory_region_add_subregion(&cfam->mr, cc->responder_offset, &slave->iomem);
    memory_region_add_subregion(&cfam->mr, cc->lbus_offset, &cfam->lbus.mr);

    cc->realize_engines(cfam, errp);
}

static void fsi_cfam_common_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_BUS;
    dc->realize = fsi_cfam_common_realize;
}

static bool fsi_cfam_realize_engines(FSICFAMCommonState *cfam, Error **errp)
{
    FSICFAMState *s = FSI_CFAM(cfam);

    /* Add scratchpad engine */
    object_initialize_child(OBJECT(s), "scratchpad", &s->scratchpad,
                            TYPE_FSI_SCRATCHPAD);

    return fsi_cfam_add_engine(cfam, DEVICE(&s->scratchpad), 0, errp);
}

static void fsi_cfam_class_init(ObjectClass *klass, const void *data)
{
    FSICFAMCommonClass *cc = FSI_CFAM_COMMON_CLASS(klass);

    cc->config = cfam_p9_config;
    cc->config_nr = ARRAY_SIZE(cfam_p9_config);
    cc->responder_offset = 0x800;
    cc->lbus_offset = 0xc00;
    cc->realize_engines = fsi_cfam_realize_engines;
}

static const TypeInfo fsi_cfam_common_info = {
    .name = TYPE_FSI_CFAM_COMMON,
    .parent = TYPE_FSI_SLAVE,
    .instance_size = sizeof(FSICFAMCommonState),
    .class_size = sizeof(FSICFAMCommonClass),
    .class_init = fsi_cfam_common_class_init,
    .abstract = true,
};

static const TypeInfo fsi_cfam_info = {
    .name = TYPE_FSI_CFAM,
    .parent = TYPE_FSI_CFAM_COMMON,
    .instance_size = sizeof(FSICFAMState),
    .class_init = fsi_cfam_class_init,
};

static void fsi_cfam_register_types(void)
{
    type_register_static(&fsi_cfam_common_info);
    type_register_static(&fsi_cfam_info);
}

type_init(fsi_cfam_register_types);
