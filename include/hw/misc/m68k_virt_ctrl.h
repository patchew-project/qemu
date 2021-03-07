/*
 * SPDX-License-Identifer: GPL-2.0-or-later
 *
 * Virt m68k system Controller
 */

#ifndef M68K_VIRT_CTRL_H
#define M68K_VIRT_CTRL_H

#define TYPE_M68K_VIRT_CTRL "m68k-virt-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(M68KVirtCtrlState, M68K_VIRT_CTRL)

struct M68KVirtCtrlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t irq_enabled;
};

#endif
