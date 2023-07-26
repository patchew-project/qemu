/*
 * Raspberry Pi 4B emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/arm/raspi_platform.h"
#include "hw/display/bcm2835_fb.h"
#include "hw/registerfields.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "qom/object.h"
#include "hw/arm/bcm2838.h"

#define TYPE_RASPI4B_MACHINE MACHINE_TYPE_NAME("raspi4b-common")
OBJECT_DECLARE_SIMPLE_TYPE(Raspi4bMachineState, RASPI4B_MACHINE)

struct Raspi4bMachineState {
    /*< private >*/
    RaspiBaseMachineState parent_obj;
    /*< public >*/
    BCM2838State soc;
    uint32_t vcram_base;
    uint32_t vcram_size;
};

static void raspi4b_machine_init(MachineState *machine)
{
    Raspi4bMachineState *s = RASPI4B_MACHINE(machine);
    RaspiBaseMachineState *s_base = RASPI_BASE_MACHINE(machine);
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    BCM2838State *soc = &s->soc;

    s_base->binfo.board_id = mc->board_rev;

    object_initialize_child(OBJECT(machine), "soc", soc,
                            board_soc_type(mc->board_rev));

    if (s->vcram_base) {
        object_property_set_uint(OBJECT(soc), "vcram-base",
                                        s->vcram_base, NULL);
    }

    if (s->vcram_size) {
        object_property_set_uint(OBJECT(soc), "vcram-size",
                                 s->vcram_size, NULL);
    }

    raspi_base_machine_init(machine, &soc->parent_obj);
}

static void get_vcram_base(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Raspi4bMachineState *ms = RASPI4B_MACHINE(obj);
    hwaddr value = ms->vcram_base;

    visit_type_uint64(v, name, &value, errp);
}

static void set_vcram_base(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Raspi4bMachineState *ms = RASPI4B_MACHINE(obj);
    hwaddr value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    ms->vcram_base = value;
}

static void get_vcram_size(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Raspi4bMachineState *ms = RASPI4B_MACHINE(obj);
    hwaddr value = ms->vcram_size;

    visit_type_uint64(v, name, &value, errp);
}

static void set_vcram_size(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Raspi4bMachineState *ms = RASPI4B_MACHINE(obj);
    hwaddr value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    ms->vcram_size = value;
}

static void raspi4b_machine_class_init(MachineClass *mc, uint32_t board_rev)
{
    object_class_property_add(OBJECT_CLASS(mc), "vcram-size", "uint32",
                              get_vcram_size, set_vcram_size, NULL, NULL);
    object_class_property_set_description(OBJECT_CLASS(mc), "vcram-size",
                                            "VideoCore RAM base address");
    object_class_property_add(OBJECT_CLASS(mc), "vcram-base", "uint32",
                              get_vcram_base, set_vcram_base, NULL, NULL);
    object_class_property_set_description(OBJECT_CLASS(mc), "vcram-base",
                                            "VideoCore RAM size");

    raspi_machine_class_common_init(mc, board_rev);
    mc->init = raspi4b_machine_init;
}

static void raspi4b1g_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    rmc->board_rev = 0xa03111;
    raspi4b_machine_class_init(mc, rmc->board_rev);
}

static void raspi4b2g_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    rmc->board_rev = 0xb03112;
    raspi4b_machine_class_init(mc, rmc->board_rev);
}

static void raspi4b4g_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    rmc->board_rev = 0xc03114;
    raspi4b_machine_class_init(mc, rmc->board_rev);
}

static void raspi4b8g_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    rmc->board_rev = 0xd03114;
    raspi4b_machine_class_init(mc, rmc->board_rev);
}

static const TypeInfo raspi4b_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("raspi4b1g"),
        .parent         = TYPE_RASPI4B_MACHINE,
        .class_init     = raspi4b1g_machine_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi4b2g"),
        .parent         = TYPE_RASPI4B_MACHINE,
        .class_init     = raspi4b2g_machine_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi4b4g"),
        .parent         = TYPE_RASPI4B_MACHINE,
        .class_init     = raspi4b4g_machine_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi4b8g"),
        .parent         = TYPE_RASPI4B_MACHINE,
        .class_init     = raspi4b8g_machine_class_init,
    }, {
        .name           = TYPE_RASPI4B_MACHINE,
        .parent         = TYPE_RASPI_BASE_MACHINE,
        .instance_size  = sizeof(Raspi4bMachineState),
        .abstract       = true,
    }
};

DEFINE_TYPES(raspi4b_machine_types)
