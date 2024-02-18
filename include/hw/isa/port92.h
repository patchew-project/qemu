/*
 * QEMU I/O port 0x92 (System Control Port A, to handle Fast Gate A20)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_PORT92_H
#define HW_PORT92_H

#include "exec/memory.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "qom/object.h"

#define TYPE_PORT92 "port92"
OBJECT_DECLARE_SIMPLE_TYPE(Port92State, PORT92)

struct Port92State {
    ISADevice parent_obj;

    MemoryRegion io;
    uint8_t outport;
    qemu_irq a20_out;
};

#define PORT92_A20_LINE "a20"

#endif /* HW_PORT92_H */
