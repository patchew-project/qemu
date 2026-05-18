
/*
 * Modified Xilinx Zynq Baseboard System emulation for Beckhoff CX7200.
 *
 * Copyright (c) 2026 Beckhoff Automation GmbH & Co. KG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/block/block.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/clock.h"
#include "qemu/error-report.h"
#include "hw/arm/xilinx_zynq.h"  /* For ZynqMachineState */
#include "hw/cpu/a9mpcore.h"
#include "hw/timer/a9gtimer.h"
#include "qom/object.h"

#define TYPE_CX7200_MACHINE MACHINE_TYPE_NAME("beckhoff-cx7200")

#define CX7200_PS_CLK_FREQUENCY (40 * 1000 * 1000)

static void ccat_init(uint32_t base, BlockBackend *eeprom_blk)
{
    DeviceState *dev;
    SysBusDevice *busdev;

    dev = qdev_new("beckhoff-ccat");
    if (eeprom_blk) {
        qdev_prop_set_drive_err(dev, "eeprom", eeprom_blk, &error_fatal);
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, base);
}

static void beckhoff_cx7200_init(MachineState *machine)
{
    DriveInfo *di;
    BlockBackend *blk;
    MachineClass *parent_mc;

    parent_mc = MACHINE_CLASS(object_class_get_parent(
                                  object_get_class(OBJECT(machine))));
    parent_mc->init(machine);

    di = drive_get(IF_NONE, 0, 0);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    ccat_init(0x40000000, blk);
}

static void beckhoff_cx7200_machine_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ZynqMachineClass *zmc = ZYNQ_MACHINE_CLASS(oc);

    mc->desc = "Beckhoff CX7200 Industrial PC (Zynq-based)";
    mc->init = beckhoff_cx7200_init;
    zmc->qspi_flash_type = "is25lp016d";
    zmc->ps_clk_freq = CX7200_PS_CLK_FREQUENCY;
}

static const TypeInfo beckhoff_cx7200_machine_type = {
    .name = TYPE_CX7200_MACHINE,
    .parent = TYPE_ZYNQ_MACHINE,
    .class_init = beckhoff_cx7200_machine_class_init,
    .instance_size = sizeof(ZynqMachineState),
};

static void beckhoff_cx7200_machine_register_types(void)
{
    type_register_static(&beckhoff_cx7200_machine_type);
}

type_init(beckhoff_cx7200_machine_register_types)
