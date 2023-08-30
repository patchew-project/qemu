/*
 * VMApple Configuration Region
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vmapple/cfg.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

static void vmapple_cfg_reset(DeviceState *dev)
{
    VMAppleCfgState *s = VMAPPLE_CFG(dev);
    VMAppleCfg *cfg;

    cfg = memory_region_get_ram_ptr(&s->mem);
    memset((void *)cfg, 0, VMAPPLE_CFG_SIZE);
    *cfg = s->cfg;
}

static void vmapple_cfg_realize(DeviceState *dev, Error **errp)
{
    VMAppleCfgState *s = VMAPPLE_CFG(dev);
    uint32_t i;

    strncpy(s->cfg.serial, s->serial, sizeof(s->cfg.serial));
    strncpy(s->cfg.model, s->model, sizeof(s->cfg.model));
    strncpy(s->cfg.soc_name, s->soc_name, sizeof(s->cfg.soc_name));
    strncpy(s->cfg.unk8, "D/A", sizeof(s->cfg.soc_name));
    s->cfg.ecid = cpu_to_be64(s->cfg.ecid);
    s->cfg.version = 2;
    s->cfg.unk1 = 1;
    s->cfg.unk2 = 1;
    s->cfg.unk3 = 0x20;
    s->cfg.unk4 = 0;
    s->cfg.unk5 = 1;
    s->cfg.unk6 = 1;
    s->cfg.unk7 = 0;
    s->cfg.unk10 = 1;

    g_assert(s->cfg.nr_cpus < ARRAY_SIZE(s->cfg.cpu_ids));
    for (i = 0; i < s->cfg.nr_cpus; i++) {
        s->cfg.cpu_ids[i] = i;
    }
}

static void vmapple_cfg_init(Object *obj)
{
    VMAppleCfgState *s = VMAPPLE_CFG(obj);

    memory_region_init_ram(&s->mem, obj, "VMApple Config", VMAPPLE_CFG_SIZE,
                           &error_fatal);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mem);

    s->serial = (char *)"1234";
    s->model = (char *)"VM0001";
    s->soc_name = (char *)"Apple M1 (Virtual)";
}

static Property vmapple_cfg_properties[] = {
    DEFINE_PROP_UINT32("nr-cpus", VMAppleCfgState, cfg.nr_cpus, 1),
    DEFINE_PROP_UINT64("ecid", VMAppleCfgState, cfg.ecid, 0),
    DEFINE_PROP_UINT64("ram-size", VMAppleCfgState, cfg.ram_size, 0),
    DEFINE_PROP_UINT32("run_installer1", VMAppleCfgState, cfg.run_installer1, 0),
    DEFINE_PROP_UINT32("run_installer2", VMAppleCfgState, cfg.run_installer2, 0),
    DEFINE_PROP_UINT32("rnd", VMAppleCfgState, cfg.rnd, 0),
    DEFINE_PROP_MACADDR("mac-en0", VMAppleCfgState, cfg.mac_en0),
    DEFINE_PROP_MACADDR("mac-en1", VMAppleCfgState, cfg.mac_en1),
    DEFINE_PROP_MACADDR("mac-wifi0", VMAppleCfgState, cfg.mac_wifi0),
    DEFINE_PROP_MACADDR("mac-bt0", VMAppleCfgState, cfg.mac_bt0),
    DEFINE_PROP_STRING("serial", VMAppleCfgState, serial),
    DEFINE_PROP_STRING("model", VMAppleCfgState, model),
    DEFINE_PROP_STRING("soc_name", VMAppleCfgState, soc_name),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmapple_cfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vmapple_cfg_realize;
    dc->desc = "VMApple Configuration Region";
    device_class_set_props(dc, vmapple_cfg_properties);
    dc->reset = vmapple_cfg_reset;
}

static const TypeInfo vmapple_cfg_info = {
    .name          = TYPE_VMAPPLE_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VMAppleCfgState),
    .instance_init = vmapple_cfg_init,
    .class_init    = vmapple_cfg_class_init,
};

static void vmapple_cfg_register_types(void)
{
    type_register_static(&vmapple_cfg_info);
}

type_init(vmapple_cfg_register_types)
