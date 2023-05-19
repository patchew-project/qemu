/*
 * QEMU PCI PS/2 adapter.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_PS2_PCI_H
#define HW_PS2_PCI_H

#include "hw/input/ps2.h"

#define TYPE_PS2_PCI "ps2-pci"

OBJECT_DECLARE_SIMPLE_TYPE(PS2PCIState, PS2_PCI)

struct PS2PCIState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion     io;

    PS2State *ps2dev;
    uint32_t cr;
    uint32_t clk;
    uint32_t last;
    int pending;
    qemu_irq irq;
    bool is_mouse;
};

#define TYPE_PS2_PCI_KBD_DEVICE "ps2-pci-keyboard"
OBJECT_DECLARE_SIMPLE_TYPE(PS2PCIKbdState, PS2_PCI_KBD_DEVICE)

struct PS2PCIKbdState {
    PS2PCIState parent_obj;

    PS2KbdState kbd;
};

#define TYPE_PS2_PCI_MOUSE_DEVICE "ps2-pci-mouse"
OBJECT_DECLARE_SIMPLE_TYPE(PS2PCIMouseState, PS2_PCI_MOUSE_DEVICE)

struct PS2PCIMouseState {
    PS2PCIState parent_obj;

    PS2MouseState mouse;
};


#endif /* HW_PS2_PCI_H */
