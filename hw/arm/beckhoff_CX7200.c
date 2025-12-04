
/*
 * Modified Xilinx Zynq Baseboard System emulation for Beckhoff CX7200.
 *
 * Copyright (c) 2024 Beckhoff Automation GmbH & Co. KG
 *
 * Based on /hw/arm/xilinx_zynq.c:
 * Copyright (c) 2010 Xilinx.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.croshtwaite@petalogix.com)
 * Copyright (c) 2012 Petalogix Pty Ltd.
 * Original code by Haibing Ma.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/block/block.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "hw/arm/xilinx_zynq.h"  /* For ZynqMachineState */
#include "hw/cpu/a9mpcore.h"
#include "qom/object.h"

#define TYPE_CX7200_MACHINE MACHINE_TYPE_NAME("beckhoff-cx7200")

#define CX7200_PERIPHCLK_DIVIDER 2
#define CX7200_PS7_CPU_CLK_FREQUENCY 720000000

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
    DeviceState *a9mpcore_dev;
    A9MPPrivState *a9mp_priv_state;

    object_property_set_str(OBJECT(machine), "flash-type", "is25lp016d",
                            &error_fatal);

    parent_mc = MACHINE_CLASS(object_class_get_parent(
                                  object_get_class(OBJECT(machine))));
    parent_mc->init(machine);

    /* Find A9MPCore and set timer frequencies directly */
    a9mpcore_dev = DEVICE(object_resolve_path_type("", TYPE_A9MPCORE_PRIV,
                                                   NULL));
    if (a9mpcore_dev) {
        a9mp_priv_state = A9MPCORE_PRIV(a9mpcore_dev);

        /* Direct struct access - devices are already realized */
        a9mp_priv_state->gtimer.freq_hz = CX7200_PS7_CPU_CLK_FREQUENCY;
        a9mp_priv_state->gtimer.periphclk_divider = CX7200_PERIPHCLK_DIVIDER;
        a9mp_priv_state->mptimer.freq_hz = CX7200_PS7_CPU_CLK_FREQUENCY;
        a9mp_priv_state->mptimer.periphclk_divider = CX7200_PERIPHCLK_DIVIDER;
        a9mp_priv_state->wdt.freq_hz = CX7200_PS7_CPU_CLK_FREQUENCY;
        a9mp_priv_state->wdt.periphclk_divider = CX7200_PERIPHCLK_DIVIDER;
    } else {
        error_setg(&error_fatal, "Could not find A9MPCore device "
                                 "for CX7200 timer configuration");
    }

    di = drive_get(IF_NONE, 0, 0);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    ccat_init(0x40000000, blk);
}

static void beckhoff_cx7200_machine_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Beckhoff CX7200 Industrial PC (Zynq-based)";
    mc->init = beckhoff_cx7200_init;
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
