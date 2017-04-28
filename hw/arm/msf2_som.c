/*
 * SmartFusion2 SOM starter kit(from Emcraft) emulation.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/arm/msf2_soc.h"
#include "hw/arm/arm.h"

static void msf2_init(MachineState *machine)
{
    DeviceState *dev;
    DeviceState *spi_flash;
    MSF2State *soc;
    DriveInfo *dinfo = drive_get_next(IF_MTD);
    qemu_irq cs_line;
    SSIBus *spi_bus;

    dev = qdev_create(NULL, TYPE_MSF2_SOC);
    qdev_prop_set_string(dev, "cpu-model", "cortex-m3");
    object_property_set_bool(OBJECT(dev), true, "realized", &error_fatal);

    soc = MSF2_SOC(dev);

    /* Attach SPI flash to SPI0 controller */
    spi_bus = (SSIBus *)qdev_get_child_bus(dev, "spi0");
    spi_flash = ssi_create_slave_no_init(spi_bus, "s25sl12801");
    qdev_prop_set_uint8(spi_flash, "spansion-cr2nv", 1);
    if (dinfo) {
        qdev_prop_set_drive(spi_flash, "drive", blk_by_legacy_dinfo(dinfo),
                                    &error_fatal);
    }
    qdev_init_nofail(spi_flash);
    cs_line = qdev_get_gpio_in_named(spi_flash, SSI_GPIO_CS, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->spi[0]), 1, cs_line);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       ENVM_SIZE);
}

static void msf2_machine_init(MachineClass *mc)
{
    mc->desc = "SmartFusion2 SOM kit from Emcraft";
    mc->init = msf2_init;
}

DEFINE_MACHINE("smartfusion2-som", msf2_machine_init)
