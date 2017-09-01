/*
 * Xilinx Zynq MPSoC PMU (Power Management Unit) emulation
 *
 * Copyright (C) 2017 Xilinx Inc
 * Written by Alistair Francis <alistair.francis@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/boards.h"
#include "cpu.h"

/* Define the PMU device */

#define TYPE_XLNX_ZYNQMP_PMU "xlnx,zynqmp-pmu"
#define XLNX_ZYNQMP_PMU(obj) OBJECT_CHECK(XlnxZynqMPPMUState, (obj), \
                                          TYPE_XLNX_ZYNQMP_PMU)

typedef struct XlnxZynqMPPMUState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
}  XlnxZynqMPPMUState;

static void xlnx_zynqmp_pmu_init(Object *obj)
{

}

static void xlnx_zynqmp_pmu_realize(DeviceState *dev, Error **errp)
{

}

static void xlnx_zynqmp_pmu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = xlnx_zynqmp_pmu_realize;
}

static const TypeInfo xlnx_zynqmp_pmu_type_info = {
    .name = TYPE_XLNX_ZYNQMP_PMU,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XlnxZynqMPPMUState),
    .instance_init = xlnx_zynqmp_pmu_init,
    .class_init = xlnx_zynqmp_pmu_class_init,
};

static void xlnx_zynqmp_pmu_register_types(void)
{
    type_register_static(&xlnx_zynqmp_pmu_type_info);
}

type_init(xlnx_zynqmp_pmu_register_types)

/* Define the PMU Machine */

static void xlnx_zcu102_pmu_init(MachineState *machine)
{

}

static void xlnx_zcu102_pmu_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP ZCU102 PMU machine";
    mc->init = xlnx_zcu102_pmu_init;
}

DEFINE_MACHINE("xlnx-zcu102-pmu", xlnx_zcu102_pmu_machine_init)

