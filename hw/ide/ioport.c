/*
 * QEMU IDE Emulation: PIO ioport registration.
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
#include "ide-internal.h"
#include "trace.h"

int ide_init_ioport(IDEBus *bus, Object *owner, int iobase, int iobase2)
{
    ISADevice *isa = ISA_DEVICE(object_dynamic_cast(owner, TYPE_ISA_DEVICE));
    int ret;

    if (isa) {
        ret = isa_register_portio_list(isa, &bus->portio_list,
                                       iobase, ide_portio_list, bus, "ide");
        if (ret == 0 && iobase2) {
            ret = isa_register_portio_list(isa, &bus->portio2_list,
                                           iobase2, ide_portio2_list, bus,
                                           "ide");
        }
        return ret;
    }

    /* PIIX3/4: no ISADevice, but ISA I/O space is set up by the PCI-ISA */
    /* bridge; register the port lists directly with the given owner.   */
    if (!isa_address_space_io(NULL)) {
        return -ENODEV;
    }
    portio_list_init(&bus->portio_list, owner, ide_portio_list, bus, "ide");
    portio_list_add(&bus->portio_list, isa_address_space_io(NULL), iobase);
    if (iobase2) {
        portio_list_init(&bus->portio2_list, owner,
                         ide_portio2_list, bus, "ide");
        portio_list_add(&bus->portio2_list,
                        isa_address_space_io(NULL), iobase2);
    }
    return 0;
}
