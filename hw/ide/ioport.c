/*
 * QEMU IDE disk and CD/DVD-ROM Emulator
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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
#include "hw/ide/isa.h"
#include "hw/ide/internal.h"
#include "trace.h"

static const MemoryRegionPortio ide_portio_list[] = {
    { 0, 8, 1, .read = ide_ioport_read, .write = ide_ioport_write },
    { 0, 1, 2, .read = ide_data_readw, .write = ide_data_writew },
    { 0, 1, 4, .read = ide_data_readl, .write = ide_data_writel },
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionPortio ide_portio2_list[] = {
    { 0, 1, 1, .read = ide_status_read, .write = ide_ctrl_write },
    PORTIO_END_OF_LIST(),
};

int ide_bus_init_ioport_isa(IDEBus *bus, ISADevice *dev,
                            int iobase, int iobase2)
{
    int ret;

    ret = isa_register_portio_list(dev, &bus->portio_list,
                                   iobase, ide_portio_list, bus, "ide");

    if (ret == 0 && iobase2) {
        ret = isa_register_portio_list(dev, &bus->portio2_list,
                                       iobase2, ide_portio2_list, bus, "ide");
    }

    return ret;
}

void ide_bus_init_ioport(IDEBus *bus, Object *owner, MemoryRegion *io,
                         int iobase, int iobase2)
{
    portio_list_register(&bus->portio_list, owner, ide_portio_list,
                         bus, "ide", io, iobase);
    portio_list_register(&bus->portio2_list, owner, ide_portio2_list,
                         bus, "ide", io, iobase2);
}
