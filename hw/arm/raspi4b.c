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
#include <libfdt.h>

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


static int raspi_add_memory_node(void *fdt, hwaddr mem_base, hwaddr mem_len)
{
    int ret;
    uint32_t acells, scells;
    char *nodename = g_strdup_printf("/memory@%" PRIx64, mem_base);

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells",
                                   NULL, &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells",
                                   NULL, &error_fatal);
    if (acells == 0 || scells == 0) {
        fprintf(stderr, "dtb file invalid (#address-cells or #size-cells 0)\n");
        ret = -1;
    } else {
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
        ret = qemu_fdt_setprop_sized_cells(fdt, nodename, "reg",
                                           acells, mem_base,
                                           scells, mem_len);
    }

    g_free(nodename);
    return ret;
}

static void raspi4_modify_dtb(const struct arm_boot_info *info, void *fdt)
{

    /* Temporary disable following devices until they are implemented*/
    const char *to_be_removed_from_dt_as_wa[] = {
        "brcm,bcm2711-thermal",
        "brcm,bcm2711-genet-v5",
    };

    for (int i = 0; i < ARRAY_SIZE(to_be_removed_from_dt_as_wa); i++) {
        const char *dev_str = to_be_removed_from_dt_as_wa[i];

        int offset = fdt_node_offset_by_compatible(fdt, -1, dev_str);
        if (offset >= 0) {
            if (!fdt_nop_node(fdt, offset)) {
                warn_report("bcm2711 dtc: %s has been disabled!", dev_str);
            }
        }
    }

    uint64_t ram_size = board_ram_size(info->board_id);

    if (ram_size > UPPER_RAM_BASE) {
        raspi_add_memory_node(fdt, UPPER_RAM_BASE, ram_size - UPPER_RAM_BASE);
    }
}

static void raspi4b_machine_init(MachineState *machine)
{
    Raspi4bMachineState *s = RASPI4B_MACHINE(machine);
    RaspiBaseMachineState *s_base = RASPI_BASE_MACHINE(machine);
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    BCM2838State *soc = &s->soc;

    s_base->binfo.modify_dtb = raspi4_modify_dtb;
    /*
     * Hack to get board revision during device tree modification without
     * changes of common code.
     * The correct way is to set board_id to MACH_TYPE_BCM2708 and add board_rev
     * to the arm_boot_info structure.
     */
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
