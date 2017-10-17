/*
 * QEMU PS/2 Controller
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_INPUT_I8042_H
#define HW_INPUT_I8042_H

#include "hw/hw.h"
#include "hw/isa/isa.h"

#define TYPE_I8042 "i8042"

#define I8042_A20_LINE "a20"

void i8042_mm_init(qemu_irq kbd_irq, qemu_irq mouse_irq,
                   MemoryRegion *region, ram_addr_t size,
                   hwaddr mask);
void i8042_isa_mouse_fake_event(void *opaque);
void i8042_setup_a20_line(ISADevice *dev, qemu_irq a20_out);

#define TYPE_VMMOUSE "vmmouse"

void vmmouse_get_data(uint32_t *data);
void vmmouse_set_data(const uint32_t *data);

#endif /* HW_INPUT_I8042_H */
