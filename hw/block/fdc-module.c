/*
 * QEMU Floppy disk emulator (Intel 82078)
 *
 * Some small helper functions which must be built into core qemu when
 * building floppy as module.
 *
 * Copyright (c) 2003, 2007 Jocelyn Mayer
 * Copyright (c) 2008 Hervé Poussineau
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
#include "hw/isa/isa.h"
#include "hw/block/fdc.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "sysemu/blockdev.h"
#include "fdc-internal.h"

void fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                        hwaddr mmio_base, DriveInfo **fds)
{
    FDCtrl *fdctrl;
    DeviceState *dev;
    SysBusDevice *sbd;
    FDCtrlSysBus *sys;

    dev = qdev_new("sysbus-fdc");
    sys = SYSBUS_FDC(dev);
    fdctrl = &sys->state;
    fdctrl->dma_chann = dma_chann; /* FIXME */
    sbd = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_connect_irq(sbd, 0, irq);
    sysbus_mmio_map(sbd, 0, mmio_base);

    fdctrl_init_drives(&sys->state.bus, fds);
}

void fdctrl_init_drives(FloppyBus *bus, DriveInfo **fds)
{
    DeviceState *dev;
    int i;

    for (i = 0; i < MAX_FD; i++) {
        if (fds[i]) {
            dev = qdev_new("floppy");
            qdev_prop_set_uint32(dev, "unit", i);
            qdev_prop_set_enum(dev, "drive-type", FLOPPY_DRIVE_TYPE_AUTO);
            qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(fds[i]),
                                    &error_fatal);
            qdev_realize_and_unref(dev, &bus->bus, &error_fatal);
        }
    }
}

void isa_fdc_init_drives(ISADevice *fdc, DriveInfo **fds)
{
    fdctrl_init_drives(&ISA_FDC(fdc)->state.bus, fds);
}

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i)
{
    FDCtrlISABus *isa = ISA_FDC(fdc);

    return isa->state.drives[i].drive;
}
